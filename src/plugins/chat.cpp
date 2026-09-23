/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/chat.cpp
 * Module:       Plugins / chat
 *
 * Purpose:      A DDial/Gtalk style chat room. One room, everyone who
 *               joins sees every line as it is typed, and the room keeps a
 *               few lines of history so somebody joining sees what was
 *               just said.
 *
 * Design:       The plugin owns each joined session, so keys come here
 *               instead of the command prompt. Lines are edited with the
 *               same line editor the shell uses, so backspace, abort and
 *               the terminal drivers all behave as usual. Incoming text is
 *               printed above the line being typed, which is then redrawn.
 *
 *                 [plugin:chat]
 *                 enabled = yes
 *                 read    = all      who may join and watch
 *                 write   = all      who may speak
 *                 admin   = sysop    who may clear the room
 *                 room    = Main
 *                 rate    = 80       lines a minute one caller may send
 *                 history = 48       lines the room remembers
 *                 mail_slots = 32    messages the board can hold at once
 *                 mail_chars = 512   longest a message may be
 *                 mail_days  = 14    how long one waits before it expires
 *
 *               The room's colours are settings too, so a board can be
 *               tuned to its own palette (any C64 colour name, see
 *               colorByName in term.cpp):
 *
 *                 color_node   = cyan       the #2 in "#2:Daytona) hi"
 *                 color_punct  = darkgrey   the : and the rank bracket
 *                 color_handle = ltgreen    the handle
 *                 color_text   = white      what was said
 *                 color_old    = darkgrey   history shown on the way in
 *                 color_notice = yellow     *** joined / left
 *                 color_room   = cyan       the banner and /s
 *                 color_private= purple     a line meant for one caller
 *
 *               In the room, a line starting with / is a command:
 *
 *                 /?            what follows
 *                 /s            who is here
 *                 /p n text     one line to node n only
 *                 /me text      an action line
 *                 /a [note]     away, or back when the note is left off
 *                 /sq n         hide a node's lines for this call
 *                 /t            the time
 *                 /clear        wipe your screen
 *                 /q            leave the room
 *                 /email h text one message to a caller, read later
 *                 /e            read the message waiting for you
 *
 *               Staff also get /k to kick a node out of the room and
 *               /b, /unb and /bans for the room ban list, which is kept
 *               in the plugin folder and survives a reboot. When no staff
 *               are in the room, three or more callers can vote somebody
 *               out with /vk: two thirds of everyone but the target, in a
 *               sixty second window. A vote can only kick, never ban.
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
#include "../core/compose.h"
#include "../core/codes.h"
#include "../core/helptext.h"
#include "info.h"
#include "../core/bbs_util.h"
#include "../core/clock.h"
#include "../core/users.h"
#include "../core/plugin.h"
#include "../platform/platform.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bbsu;

namespace {

constexpr const char kName[]     = "chat";
constexpr uint16_t   kHistDef    = 48;    // room buffer: nothing is dropped in practice
constexpr uint16_t   kHistMin    = 8;
constexpr uint16_t   kHistCeil   = 2000;  // 130 KB of line buffer: for a big board
constexpr uint8_t    kJoinShow   = 8;     // of those, how many a joiner is shown
constexpr uint8_t    kLineMax    = 64;    // one chat line, fits 40 columns twice
constexpr uint8_t    kBurst      = 8;     // lines a caller may fire off at once
constexpr uint16_t   kRateDef    = 80;    // lines a minute after that (see rate =)
constexpr uint8_t    kSlots      = BBS_MAX_NODES + 2;   // nodes, sysop, busy line
constexpr uint8_t    kBanMax     = 16;    // handles barred from the room
constexpr uint32_t   kVoteMs     = 60000; // how long a vote to kick stays open
constexpr uint8_t    kAwayMax    = 16;    // characters of an away note
// Messages the board holds at once, and how many any one person may have
// waiting.
//
// It used to be one each, and a second message REPLACED the first: somebody
// else writing to you destroyed your unread mail and the board told the
// sender it had done it. That is not a mailbox, it is a doormat, and losing
// somebody's message because a third party wrote to them is data loss
// whatever the storage costs.
//
// So: never replace, and refuse when a box is full. How full is full
// depends on where the mail lives. Internal flash is shared with accounts
// and screens and is the thing that must survive, so a board without a card
// keeps it small; a card has room and no reason to ration.
constexpr uint8_t    kMailSlots  = 64;    // messages the board holds at once
constexpr uint8_t    kMailBoxSd  = 12;    // per person, with a card
constexpr uint8_t    kMailBoxFs  = 3;     // per person, on internal flash
constexpr uint16_t   kMailChars  = 512;   // longest message, DDial had 100
constexpr uint8_t    kMailDays   = 14;    // how long one waits to be read

// ---------------------------------------------------------------------------
// Mail. One message per caller, the way DDial did it: a glorified note left
// on the door. Records live in a file in the plugin folder, one after
// another with no holes, rewritten through a temp file when anything
// changes. Only the addressees and their dates are kept in RAM, so "you
// have mail" costs no reads at all.
//
// It is not private. Anyone with the board in their hands can read the
// file, and the sysop can read it from a backup. Say so in the docs.
// ---------------------------------------------------------------------------
// MF_KEPT: read and deliberately kept, rather than waiting to be read.
//
// A kept message does not ring "You have mail", is skipped when MAIL looks
// for something new, and still counts against the box, because it is still
// taking up room somebody else cannot use.
enum : uint8_t { MF_NONE = 0, MF_KEPT = 1 };

struct MailRec {
    char     to[BBS_USER_MAX + 1];
    char     from[BBS_USER_MAX + 1];
    // flags sits in space this record was already wasting: two bytes of
    // padding before the uint32_t. So sizeof(MailRec) does not move and a
    // mail.dat written by an older build still parses, with its padding
    // zeroed by the memset every write has always done, which reads as
    // MF_NONE: not yet read. Nobody's mail is lost to the upgrade.
    uint8_t  flags;
    uint8_t  spare;
    uint32_t at;                          // epoch when it was left
    uint16_t len;
    char     text[kMailChars + 1];
};

// If either of these ever fails, the record moved and every existing
// mailbox is unreadable. Say so at compile time rather than at somebody's
// board. 564 bytes, with at at offset 44, is what every mail.dat on every
// board out there was written with.
//
// A sum of the field sizes is NOT the check. It comes to 563, because it
// misses the two bytes of padding that have always sat between from[] and
// at: exactly the two bytes flags and spare now occupy. Written that way
// the assert failed a layout that was in fact byte-for-byte correct. A file
// format is made of offsets and a total size, not of a list of field
// widths.
static_assert(sizeof(MailRec) == 564,
              "MailRec grew: old mail.dat files will not parse");
static_assert(offsetof(MailRec, at) == 44,
              "MailRec fields moved: old mail.dat files will not parse");

// The room buffer. It is claimed once when the plugin starts and given back
// when it stops, never in the BBS loop. history = in the config sizes it:
// the stock ESP32 is happy with the default, a board with more RAM can hold
// a whole evening of talk.
using ChatLine = char[kLineMax + 1];

char     g_room[20] = "Main";

// Room colours. Bright enough to tell the parts of a line apart at a
// glance, quiet enough to read for an hour. All seven are config keys.
Color    g_cNode   = Color::Cyan;          // #2
Color    g_cPunct  = Color::DarkGrey;      // : and the rank bracket
Color    g_cHandle = Color::LightGreen;    // Daytona
Color    g_cText   = Color::White;         // what they said
Color    g_cOld    = Color::DarkGrey;      // history, on the way in
Color    g_cNotice = Color::Yellow;        // *** joined, left, kicked
Color    g_cRoom   = Color::Cyan;          // banner, /s and the like
Color    g_cPriv   = Color::Purple;        // a line meant for one caller
// The P in front of a private line. Bright and separate from g_cPriv on
// purpose: the marker has to catch the eye in a scrolling room even when
// the line itself is in a quiet colour, and the two jobs are not the same.
Color    g_cPmark  = Color::LightRed;
// An action line, whole. Rob wants one colour across the lot rather than
// the node/handle/text split a said line gets, because an action is one
// sentence about somebody rather than a thing they typed.
Color    g_cAction = Color::Yellow;

// Actions are rate limited separately from ordinary lines. The room's token
// bucket is about flooding it with text; this is about the thing people
// repeat for effect. Five in thirty seconds is generous for anything meant
// seriously, and only the person doing it is told, which is the rule the
// line limit already follows.
constexpr uint8_t  kActMax    = 5;
constexpr uint32_t kActWindow = 30000;      // ms
uint8_t  g_actCount[BBS_MAX_NODES + 2] = {};
uint32_t g_actSince[BBS_MAX_NODES + 2] = {};

Color    g_cMark   = Color::Cyan;          // the --> on a line from the board

// kMark: what the board says, versus what a person said.
//
// DDial and Gtalk both marked the system's own voice so it could not be
// mistaken for somebody talking, and this room needs it more than they did
// because a chat line here has no prompt character in front of it. Without
// a marker "No such command" looks exactly like a caller typing "No such
// command".
//
// Deliberately not on everything. A join or a leave is already marked with
// *** and reads as an event rather than as an answer; the welcome screen is
// somebody's artwork and is not the board talking; and the room command
// list gets it on the heading only, because putting an arrow on all sixteen
// rows turns a table into a wall.
const char kMark[] = "--> ";

ChatLine* g_hist     = nullptr;        // g_histMax lines, allocated in start()
uint16_t g_histMax   = kHistDef;       // what the config asked for
uint16_t g_histCount = 0;              // how many of them are filled
uint16_t g_histNext  = 0;              // where the next line goes
uint8_t  g_index     = 0xFF;
uint32_t g_seq       = 0;              // lines said in the room since boot
uint16_t g_rate      = kRateDef;       // lines a minute one caller may send

char     g_mailTo[kMailSlots][BBS_USER_MAX + 1] = {};   // who has a message waiting
uint32_t g_mailAt[kMailSlots] = {};                     // and when it was left
uint8_t  g_mailFl[kMailSlots] = {};                     // MF_KEPT once read and kept

// What a caller is doing with the message they were just shown. The plugin
// owns the session while this is set, which is how a shell command gets to
// read single keys.
// MM_WRITE is the unified composer: the same screen, keys and terminators a
// forum post uses, because a caller who has learned one has learned both.
// MM_BOX is the mailbox list, a place like the forums (0.22.0), and MM_TO
// is asking who a new message is for. g_inBox says the caller came in
// through MAIL, so every decision puts them back on the list rather than at
// the shell or in the room.
enum : uint8_t { MM_NONE = 0, MM_CHOOSE, MM_REPLY, MM_WRITE, MM_BOX, MM_TO };
uint8_t  g_mailMode[BBS_MAX_NODES + 2] = {};
int16_t  g_mailIdx [BBS_MAX_NODES + 2] = {};            // the record in hand
bool     g_mailRoom[BBS_MAX_NODES + 2] = {};            // read from inside the room
bool     g_inBox   [BBS_MAX_NODES + 2] = {};            // came in through MAIL
char     g_mailWho [BBS_MAX_NODES + 2][BBS_USER_MAX + 1] = {};   // who to reply to
uint8_t  g_mailSlots = kMailSlots;                      // the board's own limit
uint16_t g_mailChars = kMailChars;

// A message being composed. The body is the only per-session buffer here and
// it is mail-sized (512) rather than forum-sized, because that is what
// MailRec holds; a bigger one would be RAM spent on something the record
// cannot store.
// The body lives on the Session, shared with the forums. Only the
// recipient is kept here, which is 21 bytes a slot.
char     g_writeTo[BBS_MAX_NODES + 2][BBS_USER_MAX + 1] = {};
compose::Body g_writeCo[BBS_MAX_NODES + 2] = {};
// Whether chat already owned this session when the editor opened. A caller
// writing from the room goes back to the room; one writing from the shell
// gets the session handed back.
bool     g_writeRoom[BBS_MAX_NODES + 2] = {};

// When the composed message is a reply, the slot of the message it answers,
// or -1. Carried through the editor so that sending the reply and retiring
// the original stay ONE rewrite of the mailbox. Two steps would mean
// choosing which way to fail, and both ways lose something.
int16_t  g_writeDrop[BBS_MAX_NODES + 2] = {};
uint8_t  g_mailDays  = kMailDays;

void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    if (!strcmp(key, "room")) snprintf(g_room, sizeof(g_room), "%.19s", value);
    else if (!strcmp(key, "rate")) {
        long v = strtol(value, nullptr, 10);
        if (v >= 6 && v <= 600) g_rate = static_cast<uint16_t>(v);
    }
    else if (!strcmp(key, "history")) {
        long v = strtol(value, nullptr, 10);
        if (v >= kHistMin && v <= kHistCeil) g_histMax = static_cast<uint16_t>(v);
    }
    else if (!strcmp(key, "color_node"))   g_cNode   = colorByName(value, g_cNode);
    else if (!strcmp(key, "color_punct"))  g_cPunct  = colorByName(value, g_cPunct);
    else if (!strcmp(key, "color_handle")) g_cHandle = colorByName(value, g_cHandle);
    else if (!strcmp(key, "color_text"))   g_cText   = colorByName(value, g_cText);
    else if (!strcmp(key, "color_old"))    g_cOld    = colorByName(value, g_cOld);
    else if (!strcmp(key, "color_notice")) g_cNotice = colorByName(value, g_cNotice);
    else if (!strcmp(key, "color_room"))   g_cRoom   = colorByName(value, g_cRoom);
    else if (!strcmp(key, "color_marker")) g_cMark   = colorByName(value, g_cMark);
    else if (!strcmp(key, "color_private")) g_cPriv  = colorByName(value, g_cPriv);
    else if (!strcmp(key, "color_pmark"))   g_cPmark = colorByName(value, g_cPmark);
    else if (!strcmp(key, "color_action"))  g_cAction = colorByName(value, g_cAction);
    else if (!strcmp(key, "mail_slots")) {
        long v = strtol(value, nullptr, 10);
        if (v >= 0 && v <= kMailSlots) g_mailSlots = static_cast<uint8_t>(v);
    }
    else if (!strcmp(key, "mail_chars")) {
        long v = strtol(value, nullptr, 10);
        if (v >= 16 && v <= kMailChars) g_mailChars = static_cast<uint16_t>(v);
    }
    else if (!strcmp(key, "mail_days")) {
        long v = strtol(value, nullptr, 10);
        if (v >= 1 && v <= 365) g_mailDays = static_cast<uint8_t>(v);
    }
}

// ---------------------------------------------------------------------------
// Rate limit. A caller gets kBurst lines to fire off, then one line back
// every 60/rate seconds. At the default that is 80 lines a minute, which is
// faster than anyone types and slow enough that nobody can flood the room.
// Only the caller who trips it is told; the room never sees it.
// ---------------------------------------------------------------------------
uint16_t g_tokens[kSlots] = {};                // sixteenths of a line
uint16_t g_lastSec[kSlots] = {};

// Per-caller room state, indexed by node. Squelch and the away note last
// for the call: they are cleared when the caller drops.
uint16_t g_squelch[kSlots] = {};               // bit per node whose lines are hidden
char     g_away[kSlots][kAwayMax + 1] = {};    // away note, empty when here

// Sticky private: every ordinary line goes to this node until /p* ends it.
// 0xFF is off. DDial had /Pn* for the same reason it is wanted here: "/p 3 "
// retyped in front of every line is nine keystrokes per line on a C64.
//
// The danger is obvious and the mitigation is not optional: somebody who
// forgets they are in it says something for one person that they meant for
// the room, or worse the reverse. The input line carries a visible [>3]
// while it is on, and leaving the room or the target leaving clears it.
uint8_t  g_sticky[kSlots];

// The bell is Session::bellOff now, shared with BELL at the main prompt.
// It lived here as g_bell and NOTHING READ IT: /b answered "Bell off." and
// changed nothing, because no line in the room ever rang. The test checked
// the answer and not the bell, which is how it lasted from 0.21.4.

// The room ban list, kept in the plugin folder so it survives a reboot.
char     g_bans[kBanMax][BBS_USER_MAX + 1] = {};

// One vote to kick at a time.
uint8_t  g_voteTarget = 0xFF;                  // node under vote, 0xFF none
uint32_t g_voteEnds   = 0;
uint16_t g_voteMask   = 0;                     // who has voted already
char     g_voteWho[BBS_USER_MAX + 1] = {};


void primeBuckets() {
    for (uint8_t i = 0; i < BBS_MAX_NODES + 2; ++i) {
        g_tokens[i]  = static_cast<uint16_t>(kBurst * 16u);
        g_lastSec[i] = 0;
    }
}

bool spendToken(const Session& s, uint32_t now) {
    uint16_t stamp = static_cast<uint16_t>(now / 1000u);             // seconds
    uint16_t* tokens = g_tokens;
    uint16_t* last   = g_lastSec;
    uint8_t slot = s.id <= BBS_MAX_NODES + 1 ? s.id : 0;
    uint16_t elapsed = static_cast<uint16_t>(stamp - last[slot]);
    if (elapsed) {
        last[slot] = stamp;
        uint32_t gain = static_cast<uint32_t>(elapsed) * g_rate * 16u / 60u;
        uint32_t now16 = tokens[slot] + gain;
        tokens[slot] = static_cast<uint16_t>(now16 > kBurst * 16u ? kBurst * 16u : now16);
    }
    if (tokens[slot] < 16u) return false;
    tokens[slot] = static_cast<uint16_t>(tokens[slot] - 16u);
    return true;
}

// slotOf: the per-caller state index. The sysop node is 0, callers are
// their node number, the busy line never gets this far.
uint8_t slotOf(const Session& s) {
    return s.id < kSlots ? s.id : 0;
}

// idOfChar: the node a line tag names, 0xFF when it is not one of ours
uint8_t idOfChar(char c) {
    if (c == 'S' || c == 's') return 0;
    if (c >= '1' && c <= '9') return static_cast<uint8_t>(c - '0');
    return 0xFF;
}

// squelched: is this line from somebody the caller has hidden? Only lines
// from a caller can be hidden; the room's own notices always get through.
bool squelched(const Session& s, const char* line) {
    if (line[0] != '#') return false;
    uint8_t from = idOfChar(line[1]);
    if (from == 0xFF || from >= kSlots) return false;
    return (g_squelch[slotOf(s)] >> from) & 1u;
}

// finder: eachSession only takes a plain function, so a small context
// carries what is being looked for and what was found.
struct Finder { uint8_t id; Session* found; char name[BBS_USER_MAX + 1]; };

// mailBusy: sitting on the [R]eply [S]ave [D]elete prompt, or typing a
// reply. The plugin owns the keys either way.
bool mailBusy(const Session& s);
// Defined below, next to the rest of the composer. Declared here
// because the [R]eply key is handled above it.
bool writeBegin(Bbs& b, Session& s, const char* to, int16_t dropIdx);
void wipeInput(Session& s);
void restoreInput(Session& s);

// joined: owns the keys AND is actually in the room. A caller who typed
// MAIL at the shell has been handed to this plugin so it can read single
// keys, and that is not the same as having walked in: they must not be
// counted in the room, and must not show up in /s.
bool joined(const Session& s) {
    if (!Bbs::instance().owns(s, g_index)) return false;
    return !mailBusy(s) || g_mailRoom[slotOf(s)];
}

// listening: in the room with nothing in front of them. Somebody deciding
// what to do with a message is not, and what is said while they decide
// waits in the ring until they are done, exactly as it does for a caller
// who is part way through typing a line.
bool listening(const Session& s) {
    return Bbs::instance().owns(s, g_index) && !mailBusy(s);
}

void matchNode(void* ctx, Session& s) {
    Finder* f = static_cast<Finder*>(ctx);
    if (!joined(s)) return;
    if (s.id == f->id) f->found = &s;
}

// matchUser: any logged-in caller with this handle, in the room or not
void matchUser(void* ctx, Session& s) {
    Finder* f = static_cast<Finder*>(ctx);
    if (!s.loggedIn || s.guest) return;
    if (ieq(s.user, f->name)) f->found = &s;
}

// inRoom: the caller sitting on that node, or null when nobody is
Session* inRoom(uint8_t id) {
    Finder f{ id, nullptr, {} };
    Bbs::instance().eachSession(matchNode, &f);
    return f.found;
}

// online: the caller logged in under this handle, anywhere on the board
Session* online(const char* handle) {
    Finder f{ 0xFF, nullptr, {} };
    snprintf(f.name, sizeof(f.name), "%.*s", BBS_USER_MAX, handle);
    Bbs::instance().eachSession(matchUser, &f);
    return f.found;
}

// tell: one line to a caller who is not in the middle of anything. The
// room commands lift their own input first and re-arm afterwards.
//
// In the room it is the board's voice, so it carries the marker. At the
// shell it is not: MAIL and /e run the same code and print the same
// sentences, and only one of the two is somebody standing in a chat room.
// Deciding it here rather than at forty-odd call sites is also what keeps
// the two from drifting apart.
void tell(Session& s, Color c, const char* text) {
    if (joined(s)) {
        s.term.color(s.tl, g_cMark);
        s.term.text(s.tl, kMark);
    }
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
    s.term.nl(s.tl);
}

// mark: the same voice for a line that builds itself rather than going
// through tell(), so the room banner and the help heading match.
void mark(Session& s) {
    s.term.color(s.tl, g_cMark);
    s.term.text(s.tl, kMark);
}

// interrupt: one line to a caller who might be part way through typing.
// Their line is lifted, the message printed, and their line put back.
void interrupt(Session& s, Color c, const char* text) {
    wipeInput(s);
    tell(s, c, text);
    restoreInput(s);
}


// remember: keep the line for whoever joins next, and number it
void remember(const char* line) {
    if (!g_hist) return;
    snprintf(g_hist[g_histNext], kLineMax + 1, "%.*s", kLineMax, line);
    g_histNext = static_cast<uint16_t>((g_histNext + 1) % g_histMax);
    if (g_histCount < g_histMax) ++g_histCount;
    ++g_seq;
}

// lineAt: room line number seq, or null once it has scrolled out of the ring
const char* lineAt(uint32_t seq) {
    if (!g_hist || seq >= g_seq || g_seq - seq > g_histCount) return nullptr;
    uint16_t back = static_cast<uint16_t>(g_seq - seq);        // 1 = the newest
    uint16_t at   = static_cast<uint16_t>((g_histNext + g_histMax - back) % g_histMax);
    return g_hist[at];
}

// who: "#2:Daytona)" - node, handle, and the bracket says the rank:
// ) a caller, * a guest, > a co-sysop, ] the sysop
char rankBracket(const Session& s) {
    char m = markFor(s);
    return m == ' ' ? ')' : m;
}

// actAllowed: a fixed window rather than a token bucket, because the limit
// is meant to be explainable. "Five every thirty seconds" is a sentence a
// caller can hold; a refill rate is not.
bool actAllowed(Session& s, uint32_t now) {
    uint8_t slot = slotOf(s);
    if (!g_actSince[slot] || now - g_actSince[slot] >= kActWindow) {
        g_actSince[slot] = now ? now : 1;
        g_actCount[slot] = 0;
    }
    if (g_actCount[slot] >= kActMax) return false;
    ++g_actCount[slot];
    return true;
}

void tag(const Session& s, char* out, size_t n) {
    snprintf(out, n, "#%s:%.20s%c", nodeName(s).t, s.user, rankBracket(s));
}

// chatPrompt: no prompt character in the room, just the cursor waiting at
// the start of the line, the way DDial and Gtalk did it
// stickyCols: columns the [>n] marker occupies, 0 when it is off.
//
// wipeInput has to erase these as well as the typed text, or every re-arm
// leaves another marker on the line. Counted here so the two cannot
// disagree.
uint8_t stickyCols(const Session& s) {
    uint8_t to = g_sticky[slotOf(s)];
    if (to == 0xFF) return 0;
    return static_cast<uint8_t>(4 + strlen(nodeNum(to).t));    // "[>" + n + "] "
}

// drawMarker: the [>n] in front of the input line while a conversation is
// stuck to one node. Its width is stickyCols(), which wipeInput erases.
void drawMarker(Session& s) {
    uint8_t to = g_sticky[slotOf(s)];
    if (to == 0xFF) return;
    // Says where the next line is going, every line, because the whole risk
    // of a sticky private is forgetting you are in one.
    char tag_[12];
    snprintf(tag_, sizeof(tag_), "[>%s] ", nodeNum(to).t);
    s.term.color(s.tl, g_cPmark);
    s.term.text(s.tl, tag_);
}

void chatPrompt(Session& s) {
    drawMarker(s);
    s.term.color(s.tl, g_cText);                             // you type in the room's text colour
    s.term.cursor(s.tl, true);
}

// restoreInput: put back exactly what wipeInput took away, the marker AND
// the half typed line. Every place that lifted the input line used to put
// back only the typed text, so after anything arrived the [>n] was gone and
// the caller no longer knew where their next line was going. Rob: "the
// prompt disappears because you print over it".
void restoreInput(Session& s) {
    drawMarker(s);
    s.term.color(s.tl, g_cText);
    s.ed.redraw(s.term, s.tl);
}

// armInput: take a line of chat from this caller. F_STAY keeps Enter on
// the same line so the line can be rewritten in its finished form.
void armInput(Session& s) {
    uint8_t room = static_cast<uint8_t>(s.term.cols() > 4 ? s.term.cols() - 2 : 32);
    s.ed.begin(room < kLineMax ? room : kLineMax, LineEditor::F_STAY);
    chatPrompt(s);
}

// wipeInput: take back whatever the caller has typed so far
void wipeInput(Session& s) {
    s.term.eraseBack(s.tl, static_cast<uint8_t>(s.ed.shown() + stickyCols(s)));
}

// ---------------------------------------------------------------------------
// showLine: one room line, each part in its own colour. A caller's line is
// "#2:Daytona) hi": node, the punctuation that carries the rank, the
// handle, then the text. Anything else is a room notice. old = true dims
// the whole line, which is how history is replayed to somebody joining.
// ---------------------------------------------------------------------------
// roomRows: a room line's text, wrapped at this reader's width by the
// @-code wrap, so a code is never split and an effect is never cut short.
// `first` is what the first row has left after anything already on it.
void roomRows(Session& s, const char* text, uint8_t first, uint8_t rest, codes::Painter& pt) {
    char row[160];
    const char* p = text;
    uint8_t cols = first ? first : 1;
    uint8_t guard = 0;
    bool any = false;
    while ((p = codes::wrap(p, row, sizeof(row), cols, pt)) != nullptr && ++guard < 8) {
        codes::row(s.term, s.tl, row, cols, pt);
        s.term.nl(s.tl);
        any = true;
        cols = rest ? rest : 1;
        if (!*p) break;
    }
    if (!any) s.term.nl(s.tl);
}

void showLine(Session& s, const char* line, bool old) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const char* colon = line[0] == '#' ? strchr(line, ':') : nullptr;
    const char* mark  = colon ? strpbrk(colon + 1, ")*>]") : nullptr;
    // An action line is "** Handle does something **": it does not match the
    // #node:handle<mark> shape, so it lands here and is drawn whole. That is
    // what Rob asked for, and it gets its own colour rather than borrowing
    // the board's notice colour, because a notice and somebody waving are
    // not the same kind of line.
    bool action = line[0] == '*' && line[1] == '*';
    // What a caller typed carries @-codes; what the board says does not.
    // History is shown plain: replaying the room's last lines to somebody
    // walking in should not replay every flourish and every bell in them.
    uint8_t w = Bbs::instance().rowWidth(s);
    if (old) {
        char plainText[160];
        codes::plain(line, plainText, sizeof(plainText));
        t.color(tl, g_cOld);
        t.text(tl, plainText);
        t.nl(tl);
        return;
    }
    if (!mark) {                                             // a notice, or an action
        if (action) {
            codes::Painter pt;
            pt.begin(g_cAction, !s.bellOff);
            roomRows(s, line, w, w, pt);
            return;
        } else {
            t.color(tl, line[0] == '#' ? g_cText : g_cNotice);
            t.text(tl, line);
        }
        t.nl(tl);
        return;
    }
    t.color(tl, g_cNode);   t.textN(tl, line, static_cast<size_t>(colon - line));
    t.color(tl, g_cPunct);  t.ch(tl, ':');
    t.color(tl, g_cHandle); t.textN(tl, colon + 1, static_cast<size_t>(mark - colon - 1));
    t.color(tl, g_cPunct);  t.ch(tl, *mark);
    // The first row has what is left after the tag; the rows after it the
    // whole width. The room used to let the terminal wrap its lines, and the
    // renderer then cut effects to a margin it believed in: a 40 column
    // reader lost the words inside an effect that crossed it, while an 80
    // column reader saw them (code review, 0.22.0).
    uint8_t used = static_cast<uint8_t>(mark + 1 - line);
    const char* text = mark + 1;
    // The space after the tag is printed here: the wrap skips leading
    // spaces, which is right for a continuation row and would have joined
    // "Daytona)" to the first word.
    if (*text == ' ') { t.color(tl, g_cText); t.ch(tl, ' '); ++text; ++used; }
    codes::Painter pt;
    pt.begin(g_cText, !s.bellOff);
    if (w > used + 8) {
        roomRows(s, text, static_cast<uint8_t>(w - used), w, pt);
    } else {                                   // a tag that leaves no room: start below it
        t.nl(tl);
        roomRows(s, text, w, w, pt);
    }
}

// flush: hand a caller everything said since they last saw the room. Only
// called when their own line is empty, so nothing ever lands in the middle
// of what somebody is typing.
void flush(Session& s) {
    if (s.ownerData >= g_seq) return;
    if (s.tl.freeBytes() < 256) return;                      // slow line: next time
    while (s.ownerData < g_seq) {
        const char* line = lineAt(s.ownerData);
        ++s.ownerData;
        if (!line) continue;                                 // older than the buffer
        if (squelched(s, line)) continue;                    // hidden by /sq
        showLine(s, line, false);
    }
    chatPrompt(s);
}

// catchUp: a caller part way through a line whose held lines are close to
// filling the buffer. Nothing is thrown away: their typing is lifted, the
// room prints, and their line goes back underneath.
void catchUp(Session& s) {
    wipeInput(s);
    flush(s);
    restoreInput(s);
}

// post: say it in the room. Callers sitting with an empty line see it now;
// anyone part way through typing gets it when they press Enter.
void post(const char* text, const Session* from) {
    remember(text);
    Session* skip = const_cast<Session*>(from);
    Bbs::instance().eachSession([](void* ctx, Session& s) {
        Session* skip = static_cast<Session*>(ctx);
        if (!listening(s)) return;
        if (&s == skip) { s.ownerData = g_seq; return; }      // they printed it themselves
        if (s.ed.len()) {                                     // mid-sentence: hold it
            if (g_seq - s.ownerData + 4u >= g_histMax) catchUp(s);  // unless the buffer is filling
            return;
        }
        flush(s);
    }, skip);
}

uint8_t roomCount() {
    uint8_t n = 0;
    Bbs::instance().eachSession([](void* ctx, Session& s) {
        if (joined(s)) ++(*static_cast<uint8_t*>(ctx));
    }, &n);
    return n;
}

// ---------------------------------------------------------------------------
// The room ban list. Sixteen handles in a plain text file in the plugin
// folder, one per line, rewritten whenever it changes. Small enough that
// reading it back on every start costs nothing.
// ---------------------------------------------------------------------------
void loadBans() {
    memset(g_bans, 0, sizeof(g_bans));
    char path[96];
    if (!plugins::path(g_index, "bans", path, sizeof(path))) return;
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[BBS_USER_MAX + 4];
    uint8_t n = 0;
    while (n < kBanMax && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len) snprintf(g_bans[n++], BBS_USER_MAX + 1, "%.*s", BBS_USER_MAX, line);
    }
    fclose(f);
}

void saveBans() {
    char path[96];
    if (!plugins::path(g_index, "bans", path, sizeof(path))) return;
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (uint8_t i = 0; i < kBanMax; ++i)
        if (g_bans[i][0]) fprintf(f, "%s\n", g_bans[i]);
    fclose(f);
}

bool isBanned(const char* handle) {
    for (uint8_t i = 0; i < kBanMax; ++i)
        if (g_bans[i][0] && ieq(g_bans[i], handle)) return true;
    return false;
}

// mailPath: the message file, or false when the plugin has no storage.
// For writing: it checks the reserve and makes the folder.
bool mailPath(char* out, size_t n) {
    return plugins::path(g_index, "mail.dat", out, n);
}

// mailReadPath: the same file, for reading. See plugins::readPath for why
// the two are different.
bool mailReadPath(char* out, size_t n) {
    return plugins::readPath(g_index, "mail.dat", out, n);
}

// mailExpired: older than the board keeps them
bool mailExpired(uint32_t at, uint32_t nowEpoch) {
    if (!at || !nowEpoch || !g_mailDays) return false;
    return (nowEpoch - at) > static_cast<uint32_t>(g_mailDays) * 86400u;
}

// mailIndex: read the addressees back into RAM. One pass, one record
// buffer, so the whole mailbox never sits in memory at once.
void mailIndex() {
    memset(g_mailTo, 0, sizeof(g_mailTo));
    memset(g_mailAt, 0, sizeof(g_mailAt));
    memset(g_mailFl, 0, sizeof(g_mailFl));
    char path[96];
    if (!mailReadPath(path, sizeof(path))) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    MailRec r;
    uint8_t n = 0;
    while (n < kMailSlots && fread(&r, sizeof(r), 1, f) == 1) {
        if (!r.to[0]) continue;
        snprintf(g_mailTo[n], BBS_USER_MAX + 1, "%.*s", BBS_USER_MAX, r.to);
        g_mailAt[n] = r.at;
        g_mailFl[n] = r.flags;
        ++n;
    }
    fclose(f);
}

// mailSlotFor: the oldest message for this handle that has not been read,
// or the oldest kept one when there is nothing new. 0xFF for none.
//
// Two passes rather than one, because MAIL should show you what has arrived
// before it shows you again what you decided to keep.
uint8_t mailSlotFor(const char* handle) {
    for (uint8_t i = 0; i < kMailSlots; ++i)
        if (g_mailTo[i][0] && ieq(g_mailTo[i], handle) && !(g_mailFl[i] & MF_KEPT))
            return i;
    for (uint8_t i = 0; i < kMailSlots; ++i)
        if (g_mailTo[i][0] && ieq(g_mailTo[i], handle)) return i;
    return 0xFF;
}

// mailNewFor: how many are waiting to be read. Kept mail is not news.
uint8_t mailNewFor(const char* handle) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMailSlots; ++i)
        if (g_mailTo[i][0] && ieq(g_mailTo[i], handle) && !(g_mailFl[i] & MF_KEPT)) ++n;
    return n;
}

uint8_t mailUsed() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMailSlots; ++i) if (g_mailTo[i][0]) ++n;
    return n;
}

// mailBoxLimit: how many messages one person may have waiting.
uint8_t mailBoxLimit() {
    return plat::sdBase()[0] ? kMailBoxSd : kMailBoxFs;
}

// mailCountFor: how many are waiting for this handle.
uint8_t mailCountFor(const char* handle) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMailSlots; ++i)
        if (g_mailTo[i][0] && ieq(g_mailTo[i], handle)) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// mailRewrite: copy the file across, keeping every record the filter says
// to keep and adding one if there is one to add. Everything goes through a
// temp file and a rename, so a power cut during a write cannot lose the
// mailbox.
// ---------------------------------------------------------------------------
// dropIdx is a position in the same order mailIndex() walks: the nth
// non-empty record in the file. It used to be a handle, which meant reading
// one message threw away every message that person had, which was invisible
// while nobody could have two.
bool mailRewrite(int16_t dropIdx, const MailRec* add, uint32_t nowEpoch,
                 int16_t markIdx = -1) {
    char path[96], tmp[112];
    if (!mailPath(path, sizeof(path))) return false;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* out = fopen(tmp, "wb");
    if (!out) return false;
    FILE* in = fopen(path, "rb");
    uint8_t kept = 0;
    if (in) {
        MailRec r;
        int16_t seen = 0;
        while (fread(&r, sizeof(r), 1, in) == 1) {
            if (!r.to[0]) continue;
            int16_t idx = seen++;
            if (dropIdx >= 0 && idx == dropIdx) continue;          // the one just read
            // The line that used to sit here dropped every record addressed
            // to the sender's recipient, so a new message quietly deleted
            // whatever was already waiting. Mail is never replaced now; a
            // full box is refused instead, which the sender can act on.
            if (mailExpired(r.at, nowEpoch)) continue;             // too old to keep
            if (kept >= g_mailSlots) continue;
            if (markIdx >= 0 && idx == markIdx) r.flags |= MF_KEPT;   // read and kept
            if (fwrite(&r, sizeof(r), 1, out) != 1) { fclose(in); fclose(out); remove(tmp); return false; }
            ++kept;
        }
        fclose(in);
    }
    if (add && kept < g_mailSlots) {
        if (fwrite(add, sizeof(*add), 1, out) != 1) { fclose(out); remove(tmp); return false; }
    }
    fclose(out);
    remove(path);
    if (rename(tmp, path) != 0) return false;
    mailIndex();
    return true;
}

// mailForget: no half-made decision outlives the call it was made in.
void mailForget(uint8_t slot) {
    if (slot >= kSlots) return;
    g_mailMode[slot]    = MM_NONE;
    g_actCount[slot]    = 0;
    g_actSince[slot]    = 0;
    // The body itself lives on the Session and is cleared by openSession, so
    // there is nothing to wipe here and no Session in hand to wipe it with.
    // Forgetting the recipient is what stops a half-written message being
    // sent to the wrong person if the slot is reused.
    g_writeTo[slot][0]   = '\0';
    g_mailIdx [slot]    = -1;
    g_mailRoom[slot]    = false;
    g_inBox   [slot]    = false;
    g_mailWho [slot][0] = '\0';
}

bool mailBusy(const Session& s) {
    return g_mailMode[slotOf(s)] != MM_NONE;
}

// mailChoosePrompt: the line that turns reading into a decision. Until one
// of these three keys is pressed the message has not been touched.
void mailChoosePrompt(Session& s) {
    s.term.color(s.tl, Color::Cyan);
    // In the mailbox the question has two more answers: Enter for the next
    // message and Q back to the list, both leaving this one as it is. The
    // short form at 40 columns drops the words, not the keys; ? says so.
    if (g_inBox[slotOf(s)])
        s.term.text(s.tl, Bbs::instance().rowWidth(s) >= 59
                              ? "[R]eply  [S]ave  [D]elete  [Enter] Next  [Q] Back: "
                              : "[R]eply [S]ave [D]elete [Q]Back: ");
    else
        s.term.text(s.tl, "[R]eply  [S]ave  [D]elete: ");
    s.term.color(s.tl, Color::White);
    s.term.cursor(s.tl, true);
}

// mailDone: the decision is made. Say what is still waiting, then put the
// caller back exactly where reading found them, which is the room if that
// is where they were and the shell prompt if it is not.
void mailBoxDraw(Session& s, bool clear);

void mailDone(Session& s) {
    uint8_t slot = slotOf(s);
    // In the mailbox, a decision puts them back on the list, under whatever
    // was just said ("Kept.", "Left for Daytona"), which is why the list is
    // not redrawn on a cleared screen here.
    if (g_inBox[slot]) {
        g_mailMode[slot]    = MM_BOX;
        g_mailIdx [slot]    = -1;
        g_mailWho [slot][0] = '\0';
        mailBoxDraw(s, false);
        return;
    }
    bool    room = g_mailRoom[slot];
    mailForget(slot);

    // Kept mail is not news, so this is the count of what has still not
    // been looked at rather than of what is in the box.
    uint8_t left = mailNewFor(s.user);
    if (left) {
        char buf[72];
        snprintf(buf, sizeof(buf), "%u more waiting. %s reads the next one.",
                 static_cast<unsigned>(left), room ? "/e" : "MAIL");
        tell(s, Color::Yellow, buf);
    }
    if (room) { flush(s); armInput(s); return; }   // back to the room, caught up
    Bbs::instance().release(s);                    // release starts its own prompt
}

// ---------------------------------------------------------------------------
// mailRead: hand a caller the message waiting for them and then ask what
// should happen to it. False when there was nothing, or when it had sat
// there too long, in which case they are told rather than left wondering.
// ---------------------------------------------------------------------------
// mailRecordAt: the record mailIndex() calls slot n: the nth non-empty
// record in the file, the same walk, so the two cannot disagree about which
// message a number means.
bool mailRecordAt(uint8_t slot, MailRec& r) {
    char path[96];
    if (!mailReadPath(path, sizeof(path))) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool found = false;
    int16_t seen = 0;
    while (fread(&r, sizeof(r), 1, f) == 1) {
        if (!r.to[0]) continue;
        if (seen++ == static_cast<int16_t>(slot)) { found = true; break; }
    }
    fclose(f);
    if (found) r.text[kMailChars] = '\0';
    return found;
}

// boxList: this caller's messages, oldest first, as slots. The position in
// this list is the number the mailbox shows and the number they type.
uint8_t boxList(const char* handle, uint8_t* out, uint8_t max) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMailSlots && n < max; ++i)
        if (g_mailTo[i][0] && ieq(g_mailTo[i], handle)) out[n++] = i;
    return n;
}

// mailSay: a line from the mailbox, at column 0 like every other line the
// board says, whether or not the caller is in the room.
void mailSay(Session& s, Color c, const char* text) {
    s.term.color(s.tl, c);
    s.term.text(s.tl, text);
    s.term.nl(s.tl);
}

// ---------------------------------------------------------------------------
// mailOpen: one message, and the question of what to do with it.
//
// The forums' post header, because mail is a message too and a caller who
// has read one has learned both: a rule with the position in it, who it is
// from, when, a rule, the body wrapped at THIS reader's width with its
// @-codes acting, and EOM. Nothing is touched until a key answers.
// ---------------------------------------------------------------------------
bool mailOpen(Session& s, uint8_t slot) {
    char buf[80];
    uint32_t nowEpoch = clk::epoch();
    if (mailExpired(g_mailAt[slot], nowEpoch)) {                   // gone stale
        mailRewrite(static_cast<int16_t>(slot), nullptr, nowEpoch);
        snprintf(buf, sizeof(buf), "A message for you expired after %u days.",
                 static_cast<unsigned>(g_mailDays));
        mailSay(s, Color::Yellow, buf);
        return false;
    }
    MailRec r;
    if (!mailRecordAt(slot, r)) return false;

    uint8_t list[kMailSlots];
    uint8_t total = boxList(s.user, list, kMailSlots);
    uint8_t pos = 0;
    for (uint8_t i = 0; i < total; ++i) if (list[i] == slot) pos = static_cast<uint8_t>(i + 1);
    bool fresh = !(g_mailFl[slot] & MF_KEPT);

    Bbs& b = Bbs::instance();
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t w = b.rowWidth(s);
    if (!w) w = 1;

    // == #2 of 4 --------------------------------- unread
    t.nl(tl);
    char id[20];
    snprintf(id, sizeof(id), "#%u of %u", static_cast<unsigned>(pos), static_cast<unsigned>(total));
    const char* state = fresh ? " unread" : "";
    t.color(tl, Color::Cyan);
    t.glyphs(tl, Glyph::HLine2, 2);
    t.ch(tl, ' ');
    t.color(tl, Color::Yellow);
    t.text(tl, id);
    t.ch(tl, ' ');
    size_t used = 2 + 1 + strlen(id) + 1;
    size_t tail = strlen(state);
    if (used + tail < w) {
        t.color(tl, Color::Cyan);
        t.glyphs(tl, Glyph::HLine, static_cast<uint8_t>(w - used - tail));
        t.color(tl, Color::LightGreen);
        t.text(tl, state);
    }
    t.nl(tl);

    char when[24] = "";
    if (r.at) clk::fmtEpoch(when, sizeof(when), "%d %b %H:%M", r.at);
    t.color(tl, Color::Cyan);  t.text(tl, "From: ");
    t.color(tl, Color::Yellow); t.text(tl, r.from);
    t.nl(tl);
    t.color(tl, Color::Cyan);  t.text(tl, "Date: ");
    t.color(tl, Color::Grey);  t.text(tl, when[0] ? when : "--");
    t.nl(tl);
    t.color(tl, Color::Cyan);
    t.glyphs(tl, Glyph::HLine, w);
    t.nl(tl);

    // Wrapped at the READER's width and through the @-code renderer, the
    // same as a forum post. A message typed at 72 columns has to read on a
    // C64, and one typed at 35 should not sit in a stripe down an 80 column
    // screen.
    {
        char line[160];
        const char* p = r.text;
        uint8_t guard = 0;
        codes::Painter pt;
        pt.begin(g_cText, !s.bellOff);
        while ((p = codes::wrap(p, line, sizeof(line), w, pt)) != nullptr && ++guard < 40) {
            pt.reserve = strlen(p) + 96;
            codes::row(t, tl, line, w, pt);
            t.nl(tl);
            if (p[-1] == '\n') codes::endParagraph(pt);
            if (!*p) break;
        }
    }
    // Rob: "at the end of messages (all) add --> EOM <--", and "Need a
    // linefeed before EOM".
    t.nl(tl);
    t.color(tl, g_cMark);
    t.text(tl, "--> EOM <--");
    t.nl(tl);
    t.nl(tl);

    // Nothing has been decided yet, so nothing has been touched. That is
    // the whole difference between a mailbox and a message that evaporates
    // the moment somebody looks at it.
    uint8_t sl   = slotOf(s);
    bool    room = b.owns(s, g_index) && !g_inBox[sl];
    if (!b.owns(s, g_index) && !b.own(s, g_index)) {
        // No way to read single keys, so there is no decision to offer.
        // Mark it kept rather than leaving it unread: unread would show
        // this same message again on the next MAIL, for ever.
        mailRewrite(-1, nullptr, nowEpoch, static_cast<int16_t>(slot));
        return true;
    }
    g_mailIdx [sl] = static_cast<int16_t>(slot);
    g_mailRoom[sl] = room;
    g_mailMode[sl] = MM_CHOOSE;
    snprintf(g_mailWho[sl], BBS_USER_MAX + 1, "%.*s", BBS_USER_MAX, r.from);
    mailChoosePrompt(s);
    return true;
}

// ---------------------------------------------------------------------------
// mailRead: the room's /e, and anything else that wants "the next one":
// the oldest not yet read, or the oldest kept when nothing is new.
// ---------------------------------------------------------------------------
bool mailRead(Session& s, bool quiet) {
    uint8_t slot = mailSlotFor(s.user);
    if (slot == 0xFF) {
        if (!quiet) mailSay(s, Color::Grey, "No mail.");
        return false;
    }
    return mailOpen(s, slot);
}

// ===========================================================================
// The mailbox: MAIL as a place (0.22.0)
//
// Rob: "Mail is still not a subsystem like forums and chat" and "we cant
// read other mail without deleting, need that full list of emails to be
// able to select and then reply." Built from the UX spec's M1 to M5
// (internal/ux-message-boards.md), with its one-column margins dropped for
// the column 0 rule.
// ===========================================================================

void mailBoxPrompt(Session& s) {
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, "Mail> ");
    s.term.color(s.tl, Color::White);
    s.ed.begin(2, 0);
}

// mailBoxDraw: the list. `clear` on the way in; not after an action, so what
// the action said ("Kept.", "Left for Daytona") stays on screen above it.
void mailBoxDraw(Session& s, bool clear) {
    Bbs& b = Bbs::instance();
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t list[kMailSlots];
    uint8_t n = boxList(s.user, list, kMailSlots);
    uint8_t fresh = 0;
    for (uint8_t i = 0; i < n; ++i) if (!(g_mailFl[list[i]] & MF_KEPT)) ++fresh;

    if (clear) t.cls(tl);
    else       t.nl(tl);
    // "4 of 12" is the whole of the storage tier: 12 with a card, 3 without,
    // the same field on both, and a full box needs no other explanation.
    char right[32], msg[64];
    snprintf(right, sizeof(right), "%u new, %u of %u", static_cast<unsigned>(fresh),
             static_cast<unsigned>(n), static_cast<unsigned>(mailBoxLimit()));
    b.rowTitle(s, "Mailbox", right);
    t.nl(tl);

    if (!n) {
        mailSay(s, Color::Cyan, "--> Nothing in your mailbox.");
        mailSay(s, Color::Cyan, "--> W writes to somebody.");
    } else {
        if (fresh) {
            snprintf(msg, sizeof(msg), "--> %u new. Enter reads the oldest.", static_cast<unsigned>(fresh));
            mailSay(s, Color::LightGreen, msg);
        } else {
            mailSay(s, Color::Grey, "--> Nothing new.");
        }
        t.nl(tl);

        // Two sequential passes over mail.dat, one open each and one record
        // buffer: the first for how wide the sender column has to be, the
        // second to draw. Holding every row at once would be a table; asking
        // for each record by position, as the first cut did, reopened the
        // file and walked it from the top for every row, twice, each open
        // paying a whole-partition space check. On a full box that was dozens
        // of filesystem traversals per keypress on the BBS task. The from
        // column is as wide as the widest sender in THIS box rather than a
        // frozen 20, because that is what buys a preview at 40 columns.
        // Timed, because opening the mailbox cost 186 ms on the board
        // (0.22.1, SYS "Loop worst ... node 1, MAIL") and nothing in this
        // code explains that much: two passes over a file of at most 64
        // records. The host cannot show a flash cost, so the board says
        // where it went, the way the loop's own slow-pass line does.
        uint32_t t0 = plat::micros();
        char path[96];
        bool have = mailReadPath(path, sizeof(path));
        uint8_t fromW = 1;
        if (have) {
            FILE* f = fopen(path, "rb");
            MailRec r;
            for (int16_t seen = 0, got = 0; f && got < n && fread(&r, sizeof(r), 1, f) == 1;) {
                if (!r.to[0]) continue;
                if (seen++ != list[got]) continue;
                ++got;
                uint8_t fl = static_cast<uint8_t>(strnlen(r.from, BBS_USER_MAX));
                if (fl > fromW) fromW = fl;
            }
            if (f) fclose(f);
        }
        uint32_t t1 = plat::micros();
        uint8_t w = b.rowWidth(s);
        int previewW = static_cast<int>(w) - 1 - 2 - 1 - fromW - 1 - 6 - 2;
        FILE* f = have ? fopen(path, "rb") : nullptr;
        int16_t seen = 0;
        for (uint8_t i = 0; i < n; ++i) {
            MailRec r;
            char from[BBS_USER_MAX + 1] = "?";
            char prev[44] = "";
            // Forward from where the last row left off: the slots are in
            // file order, so this is one walk down the file in total.
            bool found = false;
            while (f && fread(&r, sizeof(r), 1, f) == 1) {
                if (!r.to[0]) continue;
                if (seen++ == list[i]) { found = true; break; }
            }
            if (found) {
                r.text[kMailChars] = '\0';
                snprintf(from, sizeof(from), "%.*s", BBS_USER_MAX, r.from);
                codes::plain(r.text, prev, sizeof(prev));
                for (char* c = prev; *c; ++c) if (*c == '\n' || *c == '\r') *c = ' ';
            }
            bool isNew = !(g_mailFl[list[i]] & MF_KEPT);
            char num[8], date[12] = "";
            snprintf(num, sizeof(num), "%2u ", static_cast<unsigned>(i + 1));
            if (g_mailAt[list[i]]) clk::fmtEpoch(date, sizeof(date), "%d %b", g_mailAt[list[i]]);
            t.color(tl, Color::Yellow);
            t.ch(tl, isNew ? '*' : ' ');
            t.text(tl, num);
            t.color(tl, isNew ? Color::White : Color::Grey);
            t.text(tl, from);
            for (size_t k = strlen(from); k < fromW; ++k) t.ch(tl, ' ');
            t.ch(tl, ' ');
            t.color(tl, Color::Grey);
            t.text(tl, date);
            if (previewW >= 12 && prev[0]) {
                t.text(tl, "  ");
                t.color(tl, Color::DarkGrey);
                t.textCols(tl, prev, static_cast<uint8_t>(previewW > 40 ? 40 : previewW));
            }
            t.nl(tl);
        }
        if (f) fclose(f);
        uint32_t t2 = plat::micros();
        if (t2 - t0 > 20000u)
            plat::log("chat: mailbox for %s took %lu us: sizing pass %lu, drawing pass %lu, %u rows",
                      s.user, static_cast<unsigned long>(t2 - t0),
                      static_cast<unsigned long>(t1 - t0), static_cast<unsigned long>(t2 - t1),
                      static_cast<unsigned>(n));
    }
    b.rowRule(s);
    mailSay(s, Color::Grey, b.rowWidth(s) >= 60
            ? "--> A number reads it. Enter reads what is new. W writes. ? help. Q leaves."
            : "--> # reads, W writes, ? help, Q leaves");
    t.nl(tl);
    mailBoxPrompt(s);
}

void mailBoxHelp(Session& s) {
    Bbs& b = Bbs::instance();
    char right[16];
    snprintf(right, sizeof(right), "%u chars", static_cast<unsigned>(g_mailChars));
    s.term.nl(s.tl);
    b.rowTitle(s, "Mail: the keys", right);
    static const char* const kKeys[] = {
        "Enter   the oldest one not yet read",
        "1 2 3   a number reads that one",
        "W       write to somebody",
        "R S D   reply, keep, delete: reading",
        "Enter   the next one, when reading",
        "?       this",
        "Q  ESC  back, and again to leave",
    };
    for (const char* k : kKeys) mailSay(s, Color::Grey, k);
    b.rowRule(s);
    s.term.nl(s.tl);
}

// mailBoxEnter: MAIL. The plugin takes the session, the way FORUMS does.
void mailBoxEnter(Bbs& b, Session& s) {
    if (!b.owns(s, g_index) && !b.own(s, g_index)) {
        mailSay(s, Color::LightRed, "Cannot open your mailbox just now.");
        b.prompt(s);
        return;
    }
    uint8_t slot = slotOf(s);
    mailForget(slot);
    g_inBox[slot]    = true;
    g_mailMode[slot] = MM_BOX;
    b.setDoing(s, "MAIL");
    mailBoxDraw(s, true);
}

void mailBoxLeave(Session& s) {
    mailForget(slotOf(s));
    mailSay(s, Color::Grey, "Leaving your mailbox.");
    Bbs::instance().release(s);                     // release draws the prompt
}

// mailNextAfter: the next message in the box after this slot, 0xFF for none
uint8_t mailNextAfter(const char* handle, int16_t slot) {
    for (int16_t i = static_cast<int16_t>(slot + 1); i < kMailSlots; ++i)
        if (g_mailTo[i][0] && ieq(g_mailTo[i], handle)) return static_cast<uint8_t>(i);
    return 0xFF;
}

void mailAskTo(Session& s) {
    g_mailMode[slotOf(s)] = MM_TO;
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, "To: ");
    s.term.color(s.tl, Color::White);
    s.ed.begin(BBS_USER_MAX, 0);
}

void mailBoxKey(Session& s, int k) {
    uint8_t slot = slotOf(s);
    if (s.ed.len() == 0) {
        if (k == 'w' || k == 'W') { s.term.text(s.tl, "W"); s.term.nl(s.tl); mailAskTo(s); return; }
        if (k == '?') { s.term.text(s.tl, "?"); mailBoxHelp(s); mailBoxPrompt(s); return; }
        if (k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK) {
            s.term.nl(s.tl);
            mailBoxLeave(s);
            return;
        }
        if (k == KEY_ENTER || k == ' ') {
            s.term.nl(s.tl);
            uint8_t first = 0xFF;
            for (uint8_t i = 0; i < kMailSlots; ++i)
                if (g_mailTo[i][0] && ieq(g_mailTo[i], s.user) && !(g_mailFl[i] & MF_KEPT)) { first = i; break; }
            if (first == 0xFF) {
                mailSay(s, Color::Grey, "--> Nothing new. A number reads one.");
                s.term.nl(s.tl);
                mailBoxPrompt(s);
                return;
            }
            g_mailMode[slot] = MM_NONE;
            if (!mailOpen(s, first)) { g_mailMode[slot] = MM_BOX; mailBoxDraw(s, false); }
            return;
        }
        if (k < '0' || k > '9') return;             // not a key the mailbox has
    }
    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Editing) return;
    if (r == LineEditor::Res::Abort) { s.term.nl(s.tl); mailBoxPrompt(s); return; }
    uint8_t list[kMailSlots];
    uint8_t n = boxList(s.user, list, kMailSlots);
    long want = strtol(s.ed.text(), nullptr, 10);
    if (want < 1 || want > n) {
        char msg[48];
        snprintf(msg, sizeof(msg), "--> No message %ld.", want);
        mailSay(s, Color::LightRed, msg);
        s.term.nl(s.tl);
        mailBoxPrompt(s);
        return;
    }
    g_mailMode[slot] = MM_NONE;
    if (!mailOpen(s, list[want - 1])) { g_mailMode[slot] = MM_BOX; mailBoxDraw(s, false); }
}

void mailToKey(Session& s, int k) {
    uint8_t slot = slotOf(s);
    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Editing) return;
    char who[BBS_USER_MAX + 1];
    snprintf(who, sizeof(who), "%s", r == LineEditor::Res::Done ? s.ed.text() : "");
    char* w = who;
    while (*w == ' ') ++w;
    if (!*w) {                                          // ESC, or Enter on nothing
        if (r == LineEditor::Res::Abort) s.term.nl(s.tl);
        g_mailMode[slot] = MM_BOX;
        mailBoxPrompt(s);
        return;
    }
    UserRec u;
    if (users::lookup(w, u) != users::Lookup::Found) {
        char msg[64];
        snprintf(msg, sizeof(msg), "--> No account called %.20s.", w);
        mailSay(s, Color::LightRed, msg);
        s.term.nl(s.tl);
        g_mailMode[slot] = MM_BOX;
        mailBoxPrompt(s);
        return;
    }
    writeBegin(Bbs::instance(), s, u.handle, -1);
}

// ---------------------------------------------------------------------------
// mailSend: leave a message for a handle that has an account.
//
// Nothing is ever replaced. A full box is refused and the sender is told,
// which is information they can act on: wait, or find the person another
// way. Silently destroying somebody's unread mail because a third party
// wrote to them is not a trade-off, it is losing data and reporting
// success.
//
// dropIdx is the message this one answers, retired in the same rewrite that
// stores the reply. One pass over the file, one rename, so the pair either
// both happen or neither does. Doing it as two steps would mean choosing
// which to risk: delete first and a refused reply has thrown the original
// away, send first and a failed delete leaves the caller answering the same
// message again.
//
// Returns false, having said why, when nothing was stored.
// ---------------------------------------------------------------------------
bool mailSend(Session& s, const char* handle, const char* text, int16_t dropIdx = -1) {
    char buf[96];
    UserRec u;
    if (!g_mailSlots) {
        tell(s, Color::LightRed, "Mail is switched off on this board.");
        return false;
    }
    if (s.guest) {
        tell(s, Color::LightRed, "Mail needs an account.");
        return false;
    }
    if (users::lookup(handle, u) != users::Lookup::Found) {
        snprintf(buf, sizeof(buf), "No account called %.20s.", handle);
        tell(s, Color::LightRed, buf);
        return false;
    }
    uint32_t nowEpoch = clk::epoch();
    uint8_t mine = mailCountFor(u.handle);
    if (mine >= mailBoxLimit()) {
        snprintf(buf, sizeof(buf),
                 "%.20s already has %u messages waiting. Nothing was replaced.",
                 u.handle, static_cast<unsigned>(mine));
        tell(s, Color::LightRed, buf);
        return false;
    }
    // A reply takes the slot the message it answers gives back, so the
    // board's total does not grow and a full board cannot block it.
    if (dropIdx < 0 && mailUsed() >= g_mailSlots) {
        tell(s, Color::LightRed, "The board's mail is full. Try again later.");
        return false;
    }

    MailRec r;
    memset(&r, 0, sizeof(r));
    snprintf(r.to, sizeof(r.to), "%.*s", BBS_USER_MAX, u.handle);
    snprintf(r.from, sizeof(r.from), "%.*s", BBS_USER_MAX, s.user);
    r.at  = nowEpoch;
    snprintf(r.text, sizeof(r.text), "%.*s", static_cast<int>(g_mailChars), text);
    r.len = static_cast<uint16_t>(strlen(r.text));

    if (!mailRewrite(dropIdx, &r, nowEpoch)) {
        tell(s, Color::LightRed, "The message could not be stored.");
        return false;
    }
    if (mine)
        snprintf(buf, sizeof(buf), "Left for %.20s, who now has %u waiting.",
                 u.handle, static_cast<unsigned>(mine + 1));
    else
        snprintf(buf, sizeof(buf), "Left for %.20s", u.handle);
    tell(s, Color::LightGreen, buf);

    Session* now = online(u.handle);                               // tell them if they are on
    if (now && now != &s) {
        // In the room, interrupt() lifts their line and puts it back. Anywhere
        // else it goes through the core's queue, which delivers at the main
        // prompt and knows how to lift THAT. interrupt() only knew the room's
        // input line, so at the shell it wrote the notice after "[1] Main:"
        // and put the typing back with no prompt in front of it; in the forums
        // or the file areas it would have written straight across the screen.
        if (joined(*now)) interrupt(*now, Color::Yellow, "You have mail. /e reads it.");
        else              Bbs::instance().notify(*now, "You have mail. MAIL reads it.");
    }
    return true;
}

// mailWaiting: the line shown at login and on the way into the room
void mailWaiting(Session& s) {
    if (s.guest || !mailNewFor(s.user)) return;
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, "You have mail.");
    s.term.nl(s.tl);
}

void leave(Session& s, const char* why) {
    Bbs& bbs = Bbs::instance();
    char line[80], me[32];
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "*** %.28s %s", me, why);
    wipeInput(s);                                            // drop the chat input line
    bbs.release(s);                                          // the prompt starts its own line
    post(line, &s);
}

void roster(Session& s);          // defined with /s, used on the way in

void join(Bbs& bbs, Session& s) {
    char line[80];
    if (isBanned(s.user) && !s.perms) {                      // barred from this room
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "You are not welcome in the room.");
        bbs.prompt(s);
        return;
    }
    if (!bbs.own(s, g_index)) { bbs.prompt(s); return; }
    g_squelch[slotOf(s)] = 0;                                // a fresh visit hides nobody
    g_away[slotOf(s)][0] = '\0';
    g_sticky[slotOf(s)]  = 0xFF;                             // and talks to the room
    bbs.setDoing(s, "CHAT");

    Term& t = s.term;
    Timeline& tl = s.tl;
    t.reset(tl);
    // A board with screens/chatin gets a transition into the room. Without
    // one the caller lands straight on the room line as before.
    if (bbs.showScreen(s, "chatin")) t.nl(tl);
    mark(s);
    t.color(tl, g_cRoom);
    snprintf(line, sizeof(line), "%.19s: %u here. /s who, /q quits.", g_room, roomCount());
    t.text(tl, line);
    t.nl(tl);

    // Who is actually here, the same list /s gives, before anything else.
    // The count on the line above says how many; a caller walking in wants
    // to know who, and asking them to type /s to find out the one thing
    // they came in wondering is a step for nothing.
    roster(s);
    uint16_t show = g_histCount < kJoinShow ? g_histCount : kJoinShow;
    for (uint16_t i = 0; i < show; ++i) {                     // what was just said
        uint16_t at = static_cast<uint16_t>((g_histNext + g_histMax - show + i) % g_histMax);
        if (squelched(s, g_hist[at])) continue;
        showLine(s, g_hist[at], true);
    }
    s.ownerData = g_seq;                                     // the history above covers it
    if (!s.guest && mailNewFor(s.user)) {
        t.color(tl, Color::Yellow);
        t.text(tl, "You have mail. /e reads it.");
        t.nl(tl);
    }
    // Only when the board put them here. Somebody who typed CHAT knows
    // perfectly well where they are; somebody who was landed here straight
    // out of the login needs telling, because the room has no prompt
    // character and an empty line looks like a board that has stopped.
    if (s.landing) tell(s, g_cRoom, "Welcome to the chat.");

    armInput(s);

    char me[32];
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "*** %.28s joined", me);
    post(line, &s);
    // Rob: "A bell when somebody ... joins the chat room". Straight away,
    // even for somebody part way through a line whose copy of the notice
    // is being held: a bell lands between keystrokes without disturbing
    // the line, and it is the thing that makes them look up.
    Bbs::instance().eachSession([](void* ctx, Session& o) {
        if (&o == static_cast<Session*>(ctx) || !listening(o) || o.bellOff) return;
        o.term.bell(o.tl);
    }, &s);
}

// roster: the rows and the count, with nothing around them. Shared so that
// walking into the room and typing /s cannot ever show different things.
void roster(Session& s) {
    char line[80];
    Session* me = &s;
    Bbs::instance().eachSession([](void* ctx, Session& o) {
        Session* me = static_cast<Session*>(ctx);
        if (!joined(o)) return;
        char who[32], row[64];
        tag(o, who, sizeof(who));
        const char* away = g_away[slotOf(o)];
        snprintf(row, sizeof(row), "%s%s%s%.16s", who, &o == me ? " (you)" : "",
                 away[0] ? " away: " : "", away);
        showLine(*me, row, false);
    }, me);
    snprintf(line, sizeof(line), "%u in %.19s", roomCount(), g_room);
    mark(s);
    s.term.color(s.tl, g_cRoom);
    s.term.text(s.tl, line);
    s.term.nl(s.tl);
}

// who: /s . The roster, with the caller's own half-typed line lifted out of
// the way first and put back afterwards.
void who(Session& s) {
    wipeInput(s);
    roster(s);
    flush(s);
    armInput(s);
}

// ---------------------------------------------------------------------------
// notice: something the room should see that nobody said. Notices are never
// hidden by a squelch, so joining, leaving and moderation are always visible.
// ---------------------------------------------------------------------------
void notice(const char* text, const Session* from) {
    post(text, from);
}

// helpLine: one row of the /? menu
void helpLine(Session& s, const char* cmd, const char* what) {
    char row[64];
    // Fourteen: "/whois handle" is thirteen, plus one to separate it. This
    // was eleven, sized for "/email h m" when that was the widest, and the
    // commands added since cut "/whois handle" to "/whois hand" and ran
    // "/page n why" straight into its description. A column exactly as wide
    // as its widest entry is a column with no separator, the mistake
    // kUsageCol made in 0.15.0. Every description below is 25 columns or
    // fewer, so a row fits the 39 a C64 has.
    snprintf(row, sizeof(row), "%-14.14s", cmd);
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, row);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, what);
    s.term.nl(s.tl);
}

// ---------------------------------------------------------------------------
// roomHelp: /? . Staff see the moderation lines; everybody sees the vote
// only when it is the thing that would actually work.
// ---------------------------------------------------------------------------
// roomLongHelp: /? <command>. "/? p" and "/? /p" both find /p, and a word
// that is not a room command is looked up as a main prompt command, so
// "/? mail" answers too. Staff tools stay unknown to everybody else, the
// way HELP KICK does at the prompt.
bool roomLongHelp(Session& s, const char* arg) {
    char verb[16];
    size_t n = 0;
    if (arg[0] != '/') verb[n++] = '/';
    for (size_t i = 0; arg[i] && arg[i] != ' ' && n < sizeof(verb) - 1; ++i) verb[n++] = arg[i];
    verb[n] = '\0';
    // The room's own aliases first, so "/? w" explains the room's /w and
    // not WHO at the prompt, which is what the fall-through below would find.
    static const char* const kAlias[][2] = {
        { "/w", "/s" }, { "/who", "/s" }, { "/mail", "/e" }, { "/time", "/t" },
        { "/help", "/?" }, { "/h", "/?" }, { "/cls", "/clear" }, { "/info", "/i" },
        { "/intro", "/welcome" }, { "/quit", "/q" }, { "/quit+", "/q+" },
        { "/bell", "/b" }, { "/history", "/sh" }, { "/scroll", "/sh" },
        { "/pm", "/p" }, { "/msg", "/p" }, { "/em", "/email" }, { "/wi", "/whois" },
    };
    for (const auto& a : kAlias)
        if (ieq(a[0], verb)) { snprintf(verb, sizeof(verb), "%s", a[1]); break; }
    static const char* const kStaffOnly[] = { "/k", "/unb", "/bans" };
    bool staffOnly = false;
    for (const char* v : kStaffOnly) if (ieq(v, verb)) staffOnly = true;
    const char* text = (staffOnly && !s.perms) ? nullptr : helptext::find(verb);
    if (!text && !staffOnly) {
        const Command* c = Bbs::instance().findCommand(verb + 1, s);
        if (c) text = helptext::find(c->verb);
    }
    if (!text) return false;
    bool first = true;
    for (const char* p = text; *p;) {
        const char* e = strchr(p, '\n');
        size_t len = e ? static_cast<size_t>(e - p) : strlen(p);
        if (first) mark(s);
        s.term.color(s.tl, first ? Color::LightBlue : Color::Grey);
        s.term.textN(s.tl, p, len);
        s.term.nl(s.tl);
        first = false;
        p += len;
        if (*p == '\n') ++p;
    }
    return true;
}

void roomHelp(Session& s, const char* arg) {
    wipeInput(s);
    bool staff = arg && ieq(arg, "staff") && s.perms;
    if (arg && *arg && !staff) {
        if (!roomLongHelp(s, arg)) tell(s, Color::LightRed, "No such command.");
        flush(s);
        armInput(s);
        return;
    }
    mark(s);
    s.term.color(s.tl, g_cRoom);
    s.term.text(s.tl, staff ? "Staff Commands" : "Room Commands");
    s.term.nl(s.tl);
    if (staff) {
        helpLine(s, "/k n [why]", "kick a node out");
        helpLine(s, "/b handle", "bar from the room");
        helpLine(s, "/unb handle", "let them back");
        helpLine(s, "/bans", "who is barred");
        helpLine(s, "/t n +m", "give a caller minutes");
        flush(s);
        armInput(s);
        return;
    }
    helpLine(s, "/s", "who is here");
    helpLine(s, "/p n text", "one line to node n");
    helpLine(s, "/p n*", "talk to n; /p* ends it");
    helpLine(s, "/me text", "an action line");
    helpLine(s, "/page n why", "get their attention");
    helpLine(s, "/whois handle", "who is that");
    helpLine(s, "/a [note]", "away, or back again");
    helpLine(s, "/sq n", "hide or show a node");
    helpLine(s, "/email h m", "leave a message");
    helpLine(s, "/e", "read yours");
    helpLine(s, "/sh [n]", "what was said");
    helpLine(s, "/t", "time, and yours left");
    helpLine(s, "/b", "bell on or off");
    helpLine(s, "/clear", "wipe the screen");
    helpLine(s, "/welcome", "the screen you came in on");
    helpLine(s, "/i [n]", "information pages");
    helpLine(s, "/codes", "colour and effects");
    helpLine(s, "/q", "leave the room");
    helpLine(s, "/q+", "leave and log off");
    if (!s.perms) helpLine(s, "/vk n", "vote to kick (no staff)");
    helpLine(s, "/? cmd", "one command in full");
    // The staff rows on a page of their own, or a staff member's /? ran to
    // 25 lines and scrolled its own heading off a 24 line screen.
    if (s.perms) helpLine(s, "/? staff", "the staff commands");
    flush(s);
    armInput(s);
}

// ---------------------------------------------------------------------------
// staffHere: is anyone in the room able to deal with trouble? A vote to
// kick is only open when the answer is no.
// ---------------------------------------------------------------------------
struct Counter { uint8_t n; uint8_t staff; };

void countRoom(void* ctx, Session& s) {
    Counter* c = static_cast<Counter*>(ctx);
    if (!Bbs::instance().owns(s, g_index)) return;
    ++c->n;
    if (s.perms) ++c->staff;
}

Counter roomCensus() {
    Counter c{ 0, 0 };
    Bbs::instance().eachSession(countRoom, &c);
    return c;
}

// parseNode: "3" or "s" from the front of an argument, 0xFF when it is not
// a node. rest is left pointing at whatever follows.
uint8_t parseNode(const char* p, const char** rest) {
    while (*p == ' ') ++p;
    uint8_t id = 0xFF;
    if (*p == 's' || *p == 'S') { id = 0; ++p; }
    else if (*p >= '0' && *p <= '9') {
        id = static_cast<uint8_t>(*p - '0');
        ++p;
        if (*p >= '0' && *p <= '9') { id = static_cast<uint8_t>(id * 10 + (*p - '0')); ++p; }
    }
    while (*p == ' ') ++p;
    if (rest) *rest = p;
    return id;
}

// ---------------------------------------------------------------------------
// kickFromRoom: put a caller back at the command prompt with a reason. The
// room is told; the board is not: this is a room matter, not a ban.
// ---------------------------------------------------------------------------
void kickFromRoom(Session& target, const char* by, const char* why) {
    char line[96], who[32];
    tag(target, who, sizeof(who));
    wipeInput(target);
    target.term.color(target.tl, Color::LightRed);
    snprintf(line, sizeof(line), "You were removed from the room by %.20s.", by);
    target.term.text(target.tl, line);
    target.term.nl(target.tl);
    if (why && *why) {
        target.term.color(target.tl, Color::Grey);
        target.term.text(target.tl, why);
        target.term.nl(target.tl);
    }
    Bbs::instance().release(target);
    snprintf(line, sizeof(line), "*** %.28s was removed by %.16s", who, by);
    notice(line, &target);
    plat::log("chat: %s removed %s%s%s", by, target.user, why && *why ? " - " : "", why ? why : "");
}

// ---------------------------------------------------------------------------
// The vote to kick. Only open when no staff are in the room and there are
// at least three callers, and it can only remove somebody from the room.
// ---------------------------------------------------------------------------
void voteClose(const char* how) {
    char line[80];
    if (g_voteTarget == 0xFF) return;
    snprintf(line, sizeof(line), "*** vote on %.20s %s", g_voteWho, how);
    g_voteTarget = 0xFF;
    g_voteMask   = 0;
    g_voteEnds   = 0;
    notice(line, nullptr);
}

uint8_t votesNeeded(uint8_t inRoomCount) {
    uint8_t eligible = inRoomCount ? static_cast<uint8_t>(inRoomCount - 1) : 0;   // not the target
    return static_cast<uint8_t>((eligible * 2 + 2) / 3);                          // two thirds, rounded up
}

void voteKick(Session& s, const char* arg, uint32_t now) {
    char line[96];
    const char* rest = nullptr;
    uint8_t id = parseNode(arg, &rest);
    Counter c = roomCensus();

    wipeInput(s);
    if (c.staff) {
        tell(s, Color::LightRed, "Staff are here. Ask them.");
        armInput(s);
        return;
    }
    if (c.n < 3) {
        tell(s, Color::LightRed, "A vote needs three people in the room.");
        armInput(s);
        return;
    }
    Session* target = id == 0xFF ? nullptr : inRoom(id);
    if (g_voteTarget != 0xFF) target = inRoom(g_voteTarget);        // a vote is already running
    if (!target || target == &s) {
        tell(s, Color::LightRed, "/vk n, where n is the node to vote on.");
        armInput(s);
        return;
    }
    if (target->perms) {
        tell(s, Color::LightRed, "Not staff.");
        armInput(s);
        return;
    }

    if (g_voteTarget == 0xFF) {                                    // this one opens it
        g_voteTarget = target->id;
        g_voteEnds   = now + kVoteMs;
        g_voteMask   = 0;
        snprintf(g_voteWho, sizeof(g_voteWho), "%.20s", target->user);
        snprintf(line, sizeof(line), "*** vote to remove %.20s: /vk %u, %u needed",
                 target->user, static_cast<unsigned>(target->id),
                 static_cast<unsigned>(votesNeeded(c.n)));
        notice(line, nullptr);
    }
    uint16_t bit = static_cast<uint16_t>(1u << slotOf(s));
    if (g_voteMask & bit) {
        tell(s, Color::Grey, "You have voted already.");
        armInput(s);
        return;
    }
    g_voteMask = static_cast<uint16_t>(g_voteMask | bit);

    uint8_t votes = 0;
    for (uint8_t i = 0; i < kSlots; ++i) if ((g_voteMask >> i) & 1u) ++votes;
    uint8_t need = votesNeeded(c.n);
    if (votes >= need) {
        Session* t = inRoom(g_voteTarget);
        g_voteTarget = 0xFF;
        g_voteMask   = 0;
        if (t) kickFromRoom(*t, "a vote", nullptr);
    } else {
        snprintf(line, sizeof(line), "*** %u of %u votes to remove %.20s",
                 static_cast<unsigned>(votes), static_cast<unsigned>(need), g_voteWho);
        notice(line, nullptr);
    }
    flush(s);
    armInput(s);
}

// ---------------------------------------------------------------------------
// roomCommand: a line starting with /. True when it was one, which means
// the line is not said out loud.
// ---------------------------------------------------------------------------
bool roomCommand(Session& s, const char* p, uint32_t now) {
    char line[96], me[32], buf[64];
    // The verb ends at a space OR at the first digit, so "/p1 hello" and
    // "/p 1 hello" are the same command. Rob: "We should not need a space
    // there, that applies to all / commands in chat." Every room command
    // that takes a node number gets this for free, because the split is
    // here rather than in each one.
    //
    // Only a digit breaks the verb, never a letter: "/welcome" and "/quit"
    // have to keep working, and a rule that split on any non-letter would
    // turn "/?" into "/" plus something.
    // '*' breaks the verb for the same reason a digit does: "/p*" is the
    // command "/p" with "*" as its argument, and without this it parses as
    // a three character verb that matches nothing and answers "Unknown
    // command". Still never a letter, so "/welcome" and "/quit" are intact.
    const char* arg = p;
    while (*arg && *arg != ' ' && *arg != '*' && !(*arg >= '0' && *arg <= '9')) ++arg;
    size_t vlen = static_cast<size_t>(arg - p);
    while (*arg == ' ') ++arg;

    auto is = [&](const char* word) {
        return strlen(word) == vlen && !strncasecmp(p, word, vlen);
    };

    if (is("/?") || is("/help") || is("/h")) { roomHelp(s, arg); return true; }
    // /i, /i3, /i3- : the information pages, the same store INFO reads at
    // the main prompt. Writing one is INFO 3 EDIT at the prompt: the room
    // owns the keys here and the page editor needs them.
    if (is("/i") || is("/info")) {
        wipeInput(s);
        if (!*arg) {
            info::roomIndex(s);
        } else if (arg[0] >= '0' && arg[0] <= '9' && (!arg[1] || arg[1] == '-' || arg[1] == '=')) {
            uint8_t n = static_cast<uint8_t>(arg[0] - '0');
            if (arg[1] == '-')      info::roomClear(s, n);
            else if (arg[1] == '=') tell(s, Color::Grey, "INFO n EDIT at the main prompt writes a page.");
            else                    info::roomShow(s, n);
        } else {
            tell(s, Color::LightRed, "/i lists the pages, /i3 reads page 3.");
        }
        flush(s);
        armInput(s);
        return true;
    }
    if (is("/codes")) {
        wipeInput(s);
        Bbs::instance().codesSummary(s);
        flush(s);
        armInput(s);
        return true;
    }
    if (is("/q") || is("/quit"))             { leave(s, "left the room"); return true; }

    // /q+ : out of the room and off the board in one go (Rob). '+' does not
    // break a verb, so this is its own verb rather than /q with an argument.
    // The room is told here, because once logoff() has moved the session out
    // of SState::Plugin, onLogoff no longer counts it as in the room and
    // would say nothing. logoff() before release(), so release() draws no
    // prompt on the way out.
    if (is("/q+") || is("/quit+")) {
        char bye[80], who[32];
        tag(s, who, sizeof(who));
        snprintf(bye, sizeof(bye), "*** %.28s logged off", who);
        wipeInput(s);
        post(bye, &s);
        Bbs::instance().logoff(s, now);
        Bbs::instance().release(s);
        return true;
    }
    if (is("/s") || is("/w") || is("/who"))  { who(s); return true; }
    if (is("/welcome") || is("/intro")) {    // the screen you got on the way in
        if (!Bbs::instance().showScreen(s, "chatin")) {
            s.term.color(s.tl, g_cNotice);
            s.term.text(s.tl, "This board has no chat welcome screen.");
            s.term.nl(s.tl);
        }
        armInput(s);
        return true;
    }

    // /t n +m and /t n -m : give or take minutes, staff only.
    //
    // Rob asked for this in those words: "the same -1 convetion the main bbs
    // uses", which is TIME n +/-m. cmdTimeAdjust does its own permission
    // check and its own re-arming of the caller's time warnings, so the room
    // version is the same command reached from a different place rather than
    // a second implementation of granting time.
    if ((is("/t") || is("/time")) && arg && *arg) {
        wipeInput(s);
        Bbs::instance().cmdTimeAdjust(s, arg);
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/t") || is("/time")) {
        // The clock and the caller's own clock. Somebody asking the time in
        // a chat room is usually asking how long they have got, and having
        // to leave the room to find out is the wrong answer.
        char when[64] = "no clock";
        if (clk::valid()) clk::fmt(when, sizeof(when), "%a %d %b %H:%M");

        int32_t mins = Bbs::instance().minutesLeft(s, now);
        char full[128];
        if (mins < 0)
            snprintf(full, sizeof(full), "%s, no time limit", when);
        else
            snprintf(full, sizeof(full), "%s, %ld minute%s left this call",
                     when, static_cast<long>(mins), mins == 1 ? "" : "s");

        wipeInput(s);
        tell(s, g_cRoom, full);
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/clear") || is("/cls")) {
        wipeInput(s);
        s.term.cls(s.tl);
        s.ownerData = g_seq;                           // a clear screen starts fresh
        armInput(s);
        return true;
    }

    if (is("/me")) {
        if (!*arg) { wipeInput(s); tell(s, Color::Grey, "/me does what?"); armInput(s); return true; }
        wipeInput(s);
        if (!spendToken(s, now)) {
            tell(s, Color::LightRed, "Too fast.");
            armInput(s);
            return true;
        }
        if (!actAllowed(s, now)) {
            char lim[64];
            snprintf(lim, sizeof(lim), "%u actions every %u seconds. Say it instead.",
                     static_cast<unsigned>(kActMax),
                     static_cast<unsigned>(kActWindow / 1000u));
            tell(s, Color::LightRed, lim);
            armInput(s);
            return true;
        }
        flush(s);
        tag(s, me, sizeof(me));
        // ** Handle does something **, the whole line in one colour.
        // The node tag and rank bracket come off: an action is prose about
        // somebody, not a line they said, and the asterisks are what every
        // board has used to mark one since before IRC borrowed it.
        snprintf(line, sizeof(line), "** %.20s %.44s **", s.user, arg);
        showLine(s, line, false);
        post(line, &s);
        armInput(s);
        return true;
    }

    if (is("/a") || is("/away")) {
        uint8_t slot = slotOf(s);
        wipeInput(s);
        tag(s, me, sizeof(me));
        if (*arg) {
            snprintf(g_away[slot], kAwayMax + 1, "%.*s", kAwayMax, arg);
            snprintf(line, sizeof(line), "*** %.28s is away: %.16s", me, g_away[slot]);
        } else {
            g_away[slot][0] = '\0';
            snprintf(line, sizeof(line), "*** %.28s is back", me);
        }
        notice(line, nullptr);
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/sq") || is("/squelch")) {
        uint8_t id = parseNode(arg, nullptr);
        wipeInput(s);
        if (id == 0xFF || id >= kSlots || id == s.id) {
            tell(s, Color::LightRed, "/sq n, where n is a node here.");
        } else {
            uint16_t bit = static_cast<uint16_t>(1u << id);
            g_squelch[slotOf(s)] ^= bit;
            bool on = (g_squelch[slotOf(s)] & bit) != 0;
            snprintf(buf, sizeof(buf), "Node %u %s. Joining and leaving still show.",
                     static_cast<unsigned>(id), on ? "hidden" : "back");
            tell(s, on ? Color::Yellow : Color::LightGreen, buf);
        }
        flush(s);
        armInput(s);
        return true;
    }

    // /p3* and /p* : stick the conversation to one node, and end it.
    //
    // The trailing * is checked before the ordinary /p, because "/p3*" would
    // otherwise parse as a private to node 3 with "*" as the message.
    if ((is("/p") || is("/pm") || is("/msg")) && arg && *arg) {
        const char* r2 = nullptr;
        uint8_t sid = parseNode(arg, &r2);
        bool starOnly = arg[0] == '*' && arg[1] == '\0';
        bool starNode = sid != 0xFF && r2 && r2[0] == '*' && r2[1] == '\0';
        if (starOnly || starNode) {
            wipeInput(s);
            if (starOnly) {
                g_sticky[slotOf(s)] = 0xFF;
                tell(s, g_cPriv, "Back to the room.");
            } else {
                Session* to = inRoom(sid);
                if (!to || to == &s) {
                    tell(s, Color::LightRed, "Nobody on that node.");
                } else {
                    g_sticky[slotOf(s)] = sid;
                    snprintf(buf, sizeof(buf),
                             "Talking to #%s:%.16s only. /p* ends it.",
                             nodeName(*to).t, to->user);
                    tell(s, g_cPriv, buf);
                }
            }
            flush(s);
            armInput(s);
            return true;
        }
    }

    if (is("/p") || is("/pm") || is("/msg")) {
        const char* rest = nullptr;
        uint8_t id = parseNode(arg, &rest);
        Session* to = id == 0xFF ? nullptr : inRoom(id);
        wipeInput(s);
        if (!to || to == &s || !rest || !*rest) {
            tell(s, Color::LightRed, "/p n text, to somebody in the room.");
        } else if (!plugins::mayUse(s, plugins::levelFor(g_index, 1))) {
            tell(s, Color::LightRed, "You can watch, but not talk here.");
        } else if (!spendToken(s, now)) {
            tell(s, Color::LightRed, "Too fast.");
        } else {
            tag(s, me, sizeof(me));
            snprintf(line, sizeof(line), "%.28s %.60s", me, rest);
            wipeInput(*to);
            if (!to->bellOff) to->term.bell(to->tl);
            // P, not '>'. A letter says what it is without a key, and the
            // angle bracket was both dark on black and already used
            // elsewhere in the room.
            to->term.color(to->tl, g_cPmark);
            to->term.text(to->tl, "P");
            showLine(*to, line, false);
            restoreInput(*to);
            if (g_away[slotOf(*to)][0]) {
                snprintf(buf, sizeof(buf), "%.20s is away: %.16s", to->user, g_away[slotOf(*to)]);
                tell(s, Color::Grey, buf);
            }
            if (g_sticky[slotOf(s)] == id) {
                // In a stuck conversation the sender sees what they said, as
                // a line of the conversation: P> and who it went to, then the
                // words. A receipt per line ("--> /p to #2 sent." four times
                // running) told them nothing the [>n] marker had not already
                // said, and never showed the words, because the typing that
                // would have shown them was wiped to make room for the receipt.
                char them[32];
                tag(*to, them, sizeof(them));
                snprintf(line, sizeof(line), "%.28s %.60s", them, rest);
                s.term.color(s.tl, g_cPmark);
                s.term.text(s.tl, "P>");
                showLine(s, line, false);
            } else {
                // The board confirming to the sender, so it carries the
                // room's own marker and names the person as well as the node.
                snprintf(buf, sizeof(buf), "/p to #%s:%.20s sent.",
                         nodeName(*to).t, to->user);
                tell(s, g_cPriv, buf);
            }
        }
        flush(s);
        armInput(s);
        return true;
    }

    // /b : bell on or off, for this caller only. DDial had /B.
    // Bare /b only. "/b handle" is the staff bar-from-the-room command
    // further down and has been since 0.11.0, so this must not swallow it:
    // a shortcut that shadows an existing one is the FX/FILES bug, and that
    // one went unnoticed for months because nothing detects a collision.
    //
    // The split is the same shape as /t, where bare /t is the clock and
    // "/t n +m" grants minutes. /bell always means the bell.
    if (is("/bell") || (is("/b") && (!arg || !*arg))) {
        wipeInput(s);
        s.bellOff = !s.bellOff;
        tell(s, g_cRoom, s.bellOff ? "Bell off." : "Bell on.");
        flush(s);
        armInput(s);
        return true;
    }

    // /sh [n] : what the room has said, up to the whole ring.
    //
    // Joining replays a handful of lines; this is for somebody who went to
    // read their mail, or scrolled their terminal, and wants the thread
    // back. Costs nothing: the ring is already there.
    if (is("/sh") || is("/history") || is("/scroll")) {
        wipeInput(s);
        long want = arg && *arg ? strtol(arg, nullptr, 10) : 20;
        if (want < 1) want = 20;
        uint16_t show = static_cast<uint16_t>(want);
        if (show > g_histCount) show = g_histCount;
        if (!show) {
            tell(s, g_cRoom, "Nothing said yet.");
        } else {
            for (uint16_t i = 0; i < show; ++i) {
                uint16_t at = static_cast<uint16_t>((g_histNext + g_histMax - show + i) % g_histMax);
                if (squelched(s, g_hist[at])) continue;
                showLine(s, g_hist[at], true);
            }
        }
        flush(s);
        armInput(s);
        return true;
    }

    // /whois <handle> : the shell's own WHOIS, not a second copy of it.
    //
    // Deliberately NOT /info. INFO is the information pages now, and /i0 is
    // how the room reaches them; two meanings for one letter is exactly what
    // retiring "bulletin" was about. cmdInfo already hides UF_PRIVATE fields
    // from everybody but the owner and PERM_USERS staff, which is Rob's
    // condition: the same public fields PROFILE shows.
    if (is("/whois") || is("/wi")) {
        wipeInput(s);
        if (!arg || !*arg) {
            tell(s, Color::LightRed, "/whois handle");
        } else {
            Bbs::instance().cmdInfo(s, arg);
        }
        flush(s);
        armInput(s);
        return true;
    }

    // /page n <why> : ring somebody, as distinct from talking to them.
    //
    // A private message is a line in a conversation; a page is "look at your
    // screen". The room had no way to say the second, so callers used the
    // first and hoped.
    if (is("/page") || is("/pg")) {
        wipeInput(s);
        if (!arg || !*arg) {
            tell(s, Color::LightRed, "/page n why");
        } else {
            Bbs::instance().cmdPage(s, arg);
        }
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/e") || is("/mail")) {
        wipeInput(s);
        if (s.guest) tell(s, Color::LightRed, "Mail needs an account.");
        else         mailRead(s, false);
        // A message on screen owns the keys until R, S or D answers for it.
        // mailDone puts them back in the room and catches them up on what
        // was said while they were reading.
        if (mailBusy(s)) return true;
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/email") || is("/em")) {
        char handle[BBS_USER_MAX + 1];
        const char* text = arg;
        while (*text && *text != ' ') ++text;
        size_t hlen = static_cast<size_t>(text - arg);
        while (*text == ' ') ++text;
        wipeInput(s);
        if (!hlen || !*text) {
            tell(s, Color::LightRed, "/email handle your message");
        } else {
            snprintf(handle, sizeof(handle), "%.*s", static_cast<int>(hlen), arg);
            mailSend(s, handle, text);
        }
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/vk") || is("/votekick")) { voteKick(s, arg, now); return true; }

    // -- staff ---------------------------------------------------------------
    if (is("/k") || is("/kick")) {
        const char* rest = nullptr;
        uint8_t id = parseNode(arg, &rest);
        Session* to = id == 0xFF ? nullptr : inRoom(id);
        wipeInput(s);
        if (!s.perms)                       tell(s, Color::LightRed, "Staff only.");
        else if (!to || to == &s)           tell(s, Color::LightRed, "/k n, where n is a node here.");
        else if (!outranks(s, *to))         tell(s, Color::LightRed, "Not that one.");
        else                                kickFromRoom(*to, s.user, rest);
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/b") || is("/ban")) {
        wipeInput(s);
        if (!s.perms) { tell(s, Color::LightRed, "Staff only."); armInput(s); return true; }
        if (!*arg)    { tell(s, Color::LightRed, "/b handle"); armInput(s); return true; }
        if (isBanned(arg)) { tell(s, Color::Grey, "Already barred."); armInput(s); return true; }
        uint8_t slot = kBanMax;
        for (uint8_t i = 0; i < kBanMax; ++i) if (!g_bans[i][0]) { slot = i; break; }
        if (slot == kBanMax) {
            tell(s, Color::LightRed, "The room ban list is full.");
        } else {
            snprintf(g_bans[slot], BBS_USER_MAX + 1, "%.*s", BBS_USER_MAX, arg);
            saveBans();
            snprintf(line, sizeof(line), "*** %.20s is barred from the room", g_bans[slot]);
            notice(line, nullptr);
            plat::log("chat: %s barred %s", s.user, g_bans[slot]);
            for (uint8_t i = 0; i < kSlots; ++i) {         // and out they go
                Session* t = inRoom(i);
                if (t && ieq(t->user, g_bans[slot])) kickFromRoom(*t, s.user, "Barred from the room.");
            }
        }
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/unb") || is("/unban")) {
        wipeInput(s);
        if (!s.perms) { tell(s, Color::LightRed, "Staff only."); armInput(s); return true; }
        bool found = false;
        for (uint8_t i = 0; i < kBanMax; ++i) {
            if (g_bans[i][0] && ieq(g_bans[i], arg)) { g_bans[i][0] = '\0'; found = true; }
        }
        if (found) {
            saveBans();
            snprintf(buf, sizeof(buf), "%.20s may come back.", arg);
            tell(s, Color::LightGreen, buf);
            plat::log("chat: %s unbarred %s", s.user, arg);
        } else {
            tell(s, Color::Grey, "Not on the list.");
        }
        flush(s);
        armInput(s);
        return true;
    }

    if (is("/bans")) {
        wipeInput(s);
        if (!s.perms) { tell(s, Color::LightRed, "Staff only."); armInput(s); return true; }
        uint8_t n = 0;
        for (uint8_t i = 0; i < kBanMax; ++i) {
            if (!g_bans[i][0]) continue;
            snprintf(buf, sizeof(buf), "  %.20s", g_bans[i]);
            tell(s, Color::Grey, buf);
            ++n;
        }
        snprintf(buf, sizeof(buf), "%u barred from the room", static_cast<unsigned>(n));
        tell(s, g_cRoom, buf);
        flush(s);
        armInput(s);
        return true;
    }

    if (p[0] == '/') {                                   // an unknown slash line
        wipeInput(s);
        tell(s, Color::LightRed, "No such command. /? for the list.");
        flush(s);
        armInput(s);
        return true;
    }
    return false;
}

void say(Session& s, const char* text, uint32_t now) {
    char line[96], me[32];
    wipeInput(s);
    if (!plugins::mayUse(s, plugins::levelFor(g_index, 1))) {   // read-only in the room
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "You can watch, but not talk here.");
        s.term.nl(s.tl);
        armInput(s);
        return;
    }
    if (!spendToken(s, now)) {                                  // flooding the room
        char note[64];
        snprintf(note, sizeof(note), "Too fast: %u lines a minute is the limit.", g_rate);
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, note);
        s.term.nl(s.tl);
        flush(s);
        armInput(s);
        return;
    }
    flush(s);                                                   // held while they typed, first
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "%.28s %.60s", me, text);
    showLine(s, line, false);                                   // then their own line
    post(line, &s);
    armInput(s);
}

// ---------------------------------------------------------------------------
// mailChoose: one key, one outcome, and each of them is a single rewrite of
// the mailbox through a temp file and a rename. A power cut either leaves
// the message alone or leaves the decision made, never half of it.
// ---------------------------------------------------------------------------
void mailChoose(Session& s, int k) {
    uint8_t  slot = slotOf(s);
    int16_t  idx  = g_mailIdx[slot];
    uint32_t nowEpoch = clk::epoch();

    if (k == 'r' || k == 'R') {
        s.term.text(s.tl, "R");
        s.term.nl(s.tl);
        // The same editor a new message and a forum post use. This opened
        // the single-line editor before, so a reply was capped at one line
        // while everything around it was not: exactly the inconsistency the
        // shared composer exists to remove.
        //
        // idx is the slot of the message being answered, and it goes with
        // the reply so the send and the retirement stay one rewrite.
        writeBegin(Bbs::instance(), s, g_mailWho[slot], idx);
        return;
    }

    if (k == 's' || k == 'S') {
        s.term.text(s.tl, "S");
        s.term.nl(s.tl);
        // Kept, not deleted: it stays in the box, stops ringing "you have
        // mail", and still counts against the limit, because it is still
        // taking up room somebody else cannot use.
        if (idx >= 0 && mailRewrite(-1, nullptr, nowEpoch, idx))
            tell(s, Color::LightGreen, "Kept.");
        else
            tell(s, Color::LightRed, "Could not keep it. It is still unread.");
        mailDone(s);
        return;
    }

    if (k == 'd' || k == 'D') {
        s.term.text(s.tl, "D");
        s.term.nl(s.tl);
        if (idx >= 0 && mailRewrite(idx, nullptr, nowEpoch))
            tell(s, Color::Grey, "Deleted.");
        else
            tell(s, Color::LightRed, "Could not delete it.");
        mailDone(s);
        return;
    }

    // In the mailbox: Enter is the next message and Q is the list, both
    // leaving this one exactly as it is.
    if (g_inBox[slotOf(s)]) {
        if (k == KEY_ENTER || k == ' ') {
            s.term.nl(s.tl);
            uint8_t nx = mailNextAfter(s.user, idx);
            if (nx == 0xFF) { mailSay(s, Color::Grey, "--> That was the last one."); mailDone(s); return; }
            g_mailMode[slotOf(s)] = MM_NONE;
            if (!mailOpen(s, nx)) mailDone(s);
            return;
        }
        if (k == KEY_ESC || k == KEY_BREAK || k == 'q' || k == 'Q') {
            s.term.nl(s.tl);
            mailDone(s);
            return;
        }
    }

    // The abort keys and Enter leave the message alone and unread, which is
    // the outcome that loses nothing. Any other key is ignored rather than
    // guessed at, because two of the three choices here are irreversible.
    if (k == KEY_ESC || k == KEY_BREAK || k == KEY_ENTER || k == 'q' || k == 'Q') {
        s.term.nl(s.tl);
        tell(s, Color::Grey, "Left unread.");
        mailDone(s);
    }
}

// ---------------------------------------------------------------------------
// mailReplyKey: a reply is one line, the same as MAIL and /email take. The
// message being answered goes with it, in the one rewrite, which is why R
// and S are a choice rather than two things somebody does in turn.
// ---------------------------------------------------------------------------
void mailReplyKey(Session& s, int k) {
    LineEditor::Res rr = s.ed.key(k, s.term, s.tl);
    if (rr == LineEditor::Res::Editing) return;

    uint8_t slot = slotOf(s);
    if (rr == LineEditor::Res::Abort) {           // changed their mind about the reply,
        g_mailMode[slot] = MM_CHOOSE;             // not about the message
        tell(s, Color::Grey, "Reply dropped.");
        mailChoosePrompt(s);
        return;
    }

    char text[kLineMax + 1];
    snprintf(text, sizeof(text), "%s", s.ed.text());
    const char* p = text;
    while (*p == ' ') ++p;
    if (!*p) {                                    // an empty line is not a reply
        g_mailMode[slot] = MM_CHOOSE;
        mailChoosePrompt(s);
        return;
    }

    // Refused, and the original is still sitting there to try again with.
    if (!mailSend(s, g_mailWho[slot], p, g_mailIdx[slot])) {
        g_mailMode[slot] = MM_CHOOSE;
        mailChoosePrompt(s);
        return;
    }
    mailDone(s);
}

// ---------------------------------------------------------------------------
// onKey: the caller is in the room. Lines go to everyone; /q leaves.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Writing a message: the same screen the forums draw.
//
// Clear, say who it is to, then take it a line at a time. The instruction
// line names /s first and the control keys second, deliberately: /s works on
// every keyboard ever built, and a telnet client that swallows Ctrl-D would
// otherwise leave a caller with no way out of the editor.
// ---------------------------------------------------------------------------
// 16 rows, not 12. A line follows the terminal now (compose::lineWidth),
// so on a 40 column C64 a row holds about 35 characters and 12 rows would
// have capped that caller at ~420 against a 512 character allowance: the
// narrow terminal would have had a smaller mailbox than the wide one for
// no stated reason. 16 x 72 is exactly BBS_COMPOSE_MAX, so the buffer
// still cannot be overrun at the widest line.
constexpr uint8_t kWriteRows = 16;

// The "16: " the prompt draws. The rub-out loops below count the same four.
constexpr uint8_t kWritePromptCols = 4;

// writeWidth: the editor capacity and the wrap trigger, from one place, so
// they cannot drift into wrapping at a width the editor cannot hold.
inline uint8_t writeWidth(Session& s) {
    return compose::lineWidth(s.term.cols(), kWritePromptCols, BBS_LINE_MAX);
}

void writePrompt(Session& s) {
    uint8_t slot = slotOf(s);
    char q[12];
    snprintf(q, sizeof(q), "%2u: ", static_cast<unsigned>(g_writeCo[slot].rows + 1));
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, q);
    s.term.color(s.tl, Color::White);
    s.ed.begin(writeWidth(s), 0);
}

// writeBegin: open the editor, and TAKE the session so the keys arrive.
//
// Borrowing the session is not optional and is easy to forget, because
// everything else about the editor works without it: the header prints,
// the prompt appears, and then every line the caller types goes to the
// shell, which answers "Unknown command". That is exactly what happened,
// and the test only caught it once it printed what the caller saw.
//
// `room` is the case where chat already owns the session because the caller
// is in the room; then there is nothing to take and nothing to give back.
bool writeBegin(Bbs& b, Session& s, const char* to, int16_t dropIdx) {
    uint8_t slot = slotOf(s);
    // Whether the caller was in the ROOM, which is not the same question as
    // whether chat owns the session right now: reading mail from the shell
    // borrows the session, so owns() is true for somebody who was never in
    // the room. mailRead recorded the real answer, so a reply uses that.
    bool room = (dropIdx >= 0) ? g_mailRoom[slot]
                               : (b.owns(s, g_index) && !g_inBox[slot]);
    if (!b.owns(s, g_index) && !b.own(s, g_index)) {
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "Cannot open the editor just now.");
        b.prompt(s);
        return false;
    }
    g_writeRoom[slot] = room;
    g_writeDrop[slot] = dropIdx;
    snprintf(g_writeTo[slot], sizeof(g_writeTo[0]), "%s", to);
    compose::begin(g_writeCo[slot], s.compose, kMailChars, kWriteRows);
    g_mailMode[slot] = MM_WRITE;

    s.term.cls(s.tl);
    char bar[80];
    snprintf(bar, sizeof(bar), dropIdx >= 0 ? "Reply to %.30s" : "Mail to %.30s", to);
    b.rowBar(s, Color::Yellow, bar);

    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    char how[140];
    snprintf(how, sizeof(how),
             "Up to %u lines, %u characters. Long lines wrap by themselves.",
             static_cast<unsigned>(kWriteRows),
             static_cast<unsigned>(g_mailChars));
    s.term.text(s.tl, how);
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, compose::kSaveWord);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, compose::kHowToEnd);
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, compose::kDropWord);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, compose::kHowToDrop);
    s.term.nl(s.tl);
    b.rowRule(s);
    writePrompt(s);
    return true;
}

void writeFinish(Session& s, bool send) {
    uint8_t slot = slotOf(s);
    int16_t drop = g_writeDrop[slot];

    // Abandoning a REPLY goes back to the decision, not out of mail. The
    // message being answered is still sitting there undecided, and the
    // whole point of [R]eply [S]ave [D]elete is that nothing is touched
    // until a key answers it. Dropping the caller at the shell here would
    // leave the message neither kept nor read.
    if (!send && drop >= 0) {
        g_mailMode[slot] = MM_CHOOSE;
        s.compose[0]     = '\0';
        g_writeDrop[slot] = -1;
        s.term.nl(s.tl);
        tell(s, Color::Grey, "Reply dropped.");
        mailChoosePrompt(s);
        return;
    }

    g_mailMode[slot] = MM_NONE;
    s.term.nl(s.tl);
    if (!send || !g_writeCo[slot].len) {
        tell(s, Color::Grey, "Nothing sent.");
    } else if (mailSend(s, g_writeTo[slot], s.compose, drop)) {
        // mailSend says what happened, including a refusal a sender can act
        // on, so nothing is printed here on success.
    }
    s.compose[0] = '\0';
    g_writeTo[slot][0]   = '\0';
    g_writeDrop[slot]    = -1;
    if (g_inBox[slot]) {                   // back to the list they wrote from
        g_mailMode[slot] = MM_BOX;
        mailBoxDraw(s, false);
        return;
    }
    if (g_writeRoom[slot]) {
        armInput(s);                       // they were in the room already
    } else {
        Bbs::instance().release(s);        // borrowed for the editor only
        Bbs::instance().prompt(s);
    }
}

void writeKey(Session& s, int k) {
    uint8_t slot = slotOf(s);
    compose::Body& body = g_writeCo[slot];

    // Backspace on an empty line takes the previous line back for editing,
    // so a committed line is not unreachable. Repeating it walks all the way
    // back to an empty message, which is what "up to and including the
    // entire message" asks for.
    if (k == KEY_BACKSPACE && s.ed.len() == 0 && body.rows > 0) {
        char back[BBS_LINE_MAX + 1];
        compose::popLine(body, back, sizeof(back));
        // Rub out the prompt rather than starting a new line under it, or
        // the old line numbers stay on screen above the recalled one.
        // Same as the forums editor: clear this prompt, go up to the line
        // being recalled and clear it, so the text lands where it was
        // instead of a second time below it.
        uint8_t w = Bbs::instance().rowWidth(s);
        s.term.eraseBack(s.tl, kWritePromptCols);
        s.term.up(s.tl, 1);
        s.term.left(s.tl, w);
        s.term.eraseEol(s.tl, w);
        writePrompt(s);
        for (const char* c = back; *c; ++c) s.ed.key(*c, s.term, s.tl);
        return;
    }

    // Wrap while they type rather than stopping the echo dead.
    if (s.ed.len() >= writeWidth(s) && k >= ' ' && k < 0x7F) {
        char full[BBS_LINE_MAX + 1];
        snprintf(full, sizeof(full), "%s", s.ed.text());
        char carry[BBS_LINE_MAX + 1] = {};
        uint8_t keep = compose::wrapPoint(full, s.ed.len(), carry, sizeof(carry));
        full[keep] = '\0';

        // Rub the carried word off this line before it moves down; it was
        // echoed as the caller typed it and would otherwise appear twice.
        uint8_t rub = static_cast<uint8_t>(s.ed.len() - keep);
        s.term.eraseBack(s.tl, rub);
        if (!compose::addLine(body, full)) {
            s.term.nl(s.tl);
            tell(s, Color::Grey, "That is as much as one message holds. /s sends it.");
            writePrompt(s);
            return;
        }
        s.term.nl(s.tl);
        writePrompt(s);
        for (const char* c = carry; *c; ++c) s.ed.key(*c, s.term, s.tl);
    }

    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Editing) return;
    if (r == LineEditor::Res::Abort)   { writeFinish(s, false); return; }

    const char* line = s.ed.text();
    if (compose::isAbort(line)) { writeFinish(s, false); return; }
    if (compose::isSave(line))  {
        if (!body.len) {
            tell(s, Color::Grey, "Nothing written yet. /a throws it away.");
            writePrompt(s);
            return;
        }
        writeFinish(s, true);
        return;
    }

    if (!compose::addLine(body, line)) {
        tell(s, Color::Grey, "That is as much as one message holds. /s sends it.");
        writePrompt(s);
        return;
    }
    if (compose::full(body)) {
        tell(s, Color::Grey, "That is the last line. /s sends it.");
    }
    writePrompt(s);
}

void onKey(Session& s, int k, uint32_t now) {
    // A message is in hand. These keys are not chat: they decide what
    // happens to it, and they are read before the editor sees them because
    // the answer is one keypress rather than a line.
    uint8_t slot = slotOf(s);
    if (g_mailMode[slot] == MM_CHOOSE) { mailChoose(s, k);   return; }
    if (g_mailMode[slot] == MM_REPLY)  { mailReplyKey(s, k); return; }
    if (g_mailMode[slot] == MM_WRITE)  { writeKey(s, k);     return; }
    if (g_mailMode[slot] == MM_BOX)    { mailBoxKey(s, k);   return; }
    if (g_mailMode[slot] == MM_TO)     { mailToKey(s, k);    return; }

    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Abort) {
        // ESC rubs out the half-typed line; it does NOT leave the room.
        // Rob: "The escape key exits chat, that should not do that. If
        // anything esc would clear the typed line/command." He is right, and
        // it is the same rule the shell already follows, where ESC clears
        // the command line rather than hanging up. /q leaves, and the room
        // help says so.
        wipeInput(s);
        flush(s);
        armInput(s);
        return;
    }
    if (r != LineEditor::Res::Done) {
        if (!s.ed.len()) flush(s);                           // nothing typed: catch up
        return;
    }

    char line[BBS_LINE_MAX + 1];
    strncpy(line, s.ed.text(), BBS_LINE_MAX);
    line[BBS_LINE_MAX] = '\0';
    const char* p = line;
    while (*p == ' ') ++p;

    if (!*p)                     { wipeInput(s); flush(s); armInput(s); return; }
    if (roomCommand(s, p, now))  return;

    // Sticky private: this line goes to one node rather than to the room.
    //
    // Rewritten as "/p <node> <text>" and put back through the ordinary
    // command path, so the P marker, the away note, the rate limit and the
    // sender's confirmation are the same code as a typed /p. Two send paths
    // is how one of them ends up not checking the rate limit.
    //
    // If the target has left, the mode ends and the line is NOT sent
    // anywhere: saying it to the room instead would be the exact accident
    // the marker exists to prevent.
    uint8_t stick = g_sticky[slotOf(s)];
    if (stick != 0xFF) {
        if (!inRoom(stick)) {
            wipeInput(s);
            g_sticky[slotOf(s)] = 0xFF;
            tell(s, Color::LightRed, "They have gone. Back to the room, that line was not sent.");
            flush(s);
            armInput(s);
            return;
        }
        char redirect[BBS_LINE_MAX + 16];
        snprintf(redirect, sizeof(redirect), "/p %s %s", nodeNum(stick).t, p);
        roomCommand(s, redirect, now);
        return;
    }

    say(s, p, now);
}

// onLogin: the one line everybody remembers from a BBS
// ---------------------------------------------------------------------------
// onRename: a caller changed their handle, so follow it.
//
// Both of this plugin's files are keyed by handle, and before this existed a
// rename broke both of them:
//
//   - mail.dat matches on the addressee's name, so renaming somebody hid
//     their own unread mail from them. Nothing said so; the mail was simply
//     never found again.
//   - the room ban list matches on the name too, so a rename walked straight
//     out of a ban, and a handle somebody else later took inherited one.
//
// The mailbox is rewritten through the same temp file and rename the rest of
// this code uses, so a power cut leaves the old file or the new one.
// ---------------------------------------------------------------------------
void onRename(const char* oldHandle, const char* newHandle) {
    // The ban list first: it is small, in RAM, and cheap to be sure of.
    bool banMoved = false;
    for (uint8_t i = 0; i < kBanMax; ++i) {
        if (g_bans[i][0] && ieq(g_bans[i], oldHandle)) {
            snprintf(g_bans[i], BBS_USER_MAX + 1, "%.*s", BBS_USER_MAX, newHandle);
            banMoved = true;
        }
    }
    if (banMoved) {
        saveBans();
        plat::log("chat: room ban follows %s to %s", oldHandle, newHandle);
    }

    // Then the mailbox. One pass, rewriting every record that names them at
    // either end, so a reply still shows who it came from.
    char path[96], tmp[112];
    if (!mailPath(path, sizeof(path))) return;
    FILE* in = fopen(path, "rb");
    if (!in) return;                                  // no mailbox, nothing to do

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* out = fopen(tmp, "wb");
    if (!out) { fclose(in); return; }

    MailRec r;
    uint8_t moved = 0;
    bool    ok    = true;
    while (fread(&r, sizeof(r), 1, in) == 1) {
        if (r.to[0] && ieq(r.to, oldHandle)) {
            snprintf(r.to, sizeof(r.to), "%.*s", BBS_USER_MAX, newHandle);
            ++moved;
        }
        if (r.from[0] && ieq(r.from, oldHandle))
            snprintf(r.from, sizeof(r.from), "%.*s", BBS_USER_MAX, newHandle);
        if (fwrite(&r, sizeof(r), 1, out) != 1) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) { remove(tmp); return; }                 // leave the old file alone

    remove(path);
    if (rename(tmp, path) != 0) return;
    mailIndex();                                      // the in-RAM index names them too
    if (moved) plat::log("chat: %u message%s follow%s %s to %s",
                         static_cast<unsigned>(moved), moved == 1 ? "" : "s",
                         moved == 1 ? "s" : "", oldHandle, newHandle);
}

void onLogin(Session& s) {
    mailWaiting(s);
}

void onLogoff(Session& s) {
    uint8_t slot = slotOf(s);
    bool wasIn = joined(s);                 // asked before the mail state is cleared
    mailForget(slot);
    g_squelch[slot] = 0;
    g_away[slot][0] = '\0';
    for (uint8_t i = 0; i < kSlots; ++i)                    // nobody inherits a squelch
        g_squelch[i] = static_cast<uint16_t>(g_squelch[i] & ~(1u << slot));
    if (g_voteTarget == s.id) voteClose("ended, they left");
    if (!wasIn) return;                     // reading mail at the shell is not the room
    char line[80], me[32];
    tag(s, me, sizeof(me));
    snprintf(line, sizeof(line), "*** %.28s logged off", me);
    post(line, &s);
}

bool start(Bbs& bbs) {
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_histCount = g_histNext = 0;
    g_seq = 0;
    g_histMax = kHistDef;
    plugins::forEachKey(g_index, readKey, nullptr);

    // One allocation, at start, for the whole room buffer. If the board
    // cannot spare what the config asked for, fall back to the default
    // rather than refusing to open the room.
    g_hist = static_cast<ChatLine*>(calloc(g_histMax, sizeof(ChatLine)));
    if (!g_hist && g_histMax > kHistDef) {
        plat::log("chat: history = %u did not fit, using %u",
                  static_cast<unsigned>(g_histMax), static_cast<unsigned>(kHistDef));
        g_histMax = kHistDef;
        g_hist = static_cast<ChatLine*>(calloc(g_histMax, sizeof(ChatLine)));
    }
    if (!g_hist) return false;                               // no room, no room
    primeBuckets();
    memset(g_squelch, 0, sizeof(g_squelch));
    memset(g_away, 0, sizeof(g_away));
    g_voteTarget = 0xFF;
    g_voteMask   = 0;
    loadBans();
    mailIndex();
    return true;
}

// tick: the only thing with a clock in the room is the vote window
void tick(uint32_t now) {
    if (g_voteTarget == 0xFF) return;
    if (static_cast<int32_t>(now - g_voteEnds) < 0) return;
    voteClose("ran out of time");
}

void stop() {
    free(g_hist);
    g_hist = nullptr;
    g_histCount = g_histNext = 0;
    // A config save stops and starts every plugin, and the core hands any
    // caller sitting inside one back to the prompt first. Leaving MM_CHOOSE
    // set would stand them at a prompt nothing is listening to.
    for (uint8_t i = 0; i < kSlots; ++i) mailForget(i);
}

const Command kCommands[] = {
    { "CHAT", "", 0, CF_READ, "CHAT", "join the chat room",
      [](Bbs& b, Session& s, const char*, uint32_t) { join(b, s); },
      Menu::Chat, 0 },
    { "MAIL", "", 0, CF_WRITE | CF_ACCOUNT, "MAIL [h] [m]", "read or leave a message",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          if (!*a) { mailBoxEnter(b, s); return; }
          char handle[BBS_USER_MAX + 1];
          const char* text = a;
          while (*text && *text != ' ') ++text;
          size_t hlen = static_cast<size_t>(text - a);
          while (*text == ' ') ++text;
          if (!hlen) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "MAIL handle, or MAIL handle your message");
              b.prompt(s);
              return;
          }
          snprintf(handle, sizeof(handle), "%.*s", static_cast<int>(hlen), a);
          if (!*text) {
              // No message on the line, so open the full editor. This is
              // the shape Rob asked for: one message entry everywhere, with
              // the one-line form kept because it is quick.
              UserRec u;
              if (users::lookup(handle, u) != users::Lookup::Found) {
                  s.term.color(s.tl, Color::LightRed);
                  s.term.text(s.tl, "No account by that name.");
                  b.prompt(s);
                  return;
              }
              writeBegin(b, s, u.handle, -1);
              return;
          }
          mailSend(s, handle, text);
          b.prompt(s);
      },
      Menu::Chat, 1 },
    { "CHATCLEAR", "", 0, CF_ADMIN, "CHATCLEAR", "clear the chat history",
      [](Bbs& b, Session& s, const char*, uint32_t) {
          g_histCount = g_histNext = 0;
          s.term.color(s.tl, Color::Yellow);
          s.term.text(s.tl, "Chat history cleared.");
          b.prompt(s);
      },
      Menu::Chat, 30 },
};

} // namespace

extern const Plugin kChatPlugin = {
    // on unless switched off, and everyone may talk unless system.cfg says otherwise
    { kName, "Chat room", "1.0", 0, 0, PF_CORE | PF_ON, PlugLevel::All, PlugLevel::All, PlugLevel::Sysop },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    onLogin,
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
    onRename,
    nullptr,                 // listDone
};
