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
enum : uint8_t { MM_NONE = 0, MM_CHOOSE, MM_REPLY };
uint8_t  g_mailMode[BBS_MAX_NODES + 2] = {};
int16_t  g_mailIdx [BBS_MAX_NODES + 2] = {};            // the record in hand
bool     g_mailRoom[BBS_MAX_NODES + 2] = {};            // read from inside the room
char     g_mailWho [BBS_MAX_NODES + 2][BBS_USER_MAX + 1] = {};   // who to reply to
uint8_t  g_mailSlots = kMailSlots;                      // the board's own limit
uint16_t g_mailChars = kMailChars;
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
    s.term.eraseBack(s.tl, s.ed.shown());
    tell(s, c, text);
    s.ed.redraw(s.term, s.tl);
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

void tag(const Session& s, char* out, size_t n) {
    snprintf(out, n, "#%s:%.20s%c", nodeName(s).t, s.user, rankBracket(s));
}

// chatPrompt: no prompt character in the room, just the cursor waiting at
// the start of the line, the way DDial and Gtalk did it
void chatPrompt(Session& s) {
    s.term.color(s.tl, g_cText);                             // you type in the room's text colour
    s.term.cursor(s.tl, true);
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
    s.term.eraseBack(s.tl, s.ed.shown());
}

// ---------------------------------------------------------------------------
// showLine: one room line, each part in its own colour. A caller's line is
// "#2:Daytona) hi": node, the punctuation that carries the rank, the
// handle, then the text. Anything else is a room notice. old = true dims
// the whole line, which is how history is replayed to somebody joining.
// ---------------------------------------------------------------------------
void showLine(Session& s, const char* line, bool old) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const char* colon = line[0] == '#' ? strchr(line, ':') : nullptr;
    const char* mark  = colon ? strpbrk(colon + 1, ")*>]") : nullptr;
    if (old || !mark) {                                      // history, or a notice
        t.color(tl, old ? g_cOld : (line[0] == '#' ? g_cText : g_cNotice));
        t.text(tl, line);
        t.nl(tl);
        return;
    }
    t.color(tl, g_cNode);   t.textN(tl, line, static_cast<size_t>(colon - line));
    t.color(tl, g_cPunct);  t.ch(tl, ':');
    t.color(tl, g_cHandle); t.textN(tl, colon + 1, static_cast<size_t>(mark - colon - 1));
    t.color(tl, g_cPunct);  t.ch(tl, *mark);
    t.color(tl, g_cText);   t.text(tl, mark + 1);
    t.nl(tl);
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
    s.ed.redraw(s.term, s.tl);
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

// mailPath: the message file, or false when the plugin has no storage
bool mailPath(char* out, size_t n) {
    return plugins::path(g_index, "mail.dat", out, n);
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
    if (!mailPath(path, sizeof(path))) return;
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
    g_mailIdx [slot]    = -1;
    g_mailRoom[slot]    = false;
    g_mailWho [slot][0] = '\0';
}

bool mailBusy(const Session& s) {
    return g_mailMode[slotOf(s)] != MM_NONE;
}

// mailChoosePrompt: the line that turns reading into a decision. Until one
// of these three keys is pressed the message has not been touched.
void mailChoosePrompt(Session& s) {
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, "[R]eply  [S]ave  [D]elete: ");
    s.term.color(s.tl, Color::White);
    s.term.cursor(s.tl, true);
}

// mailDone: the decision is made. Say what is still waiting, then put the
// caller back exactly where reading found them, which is the room if that
// is where they were and the shell prompt if it is not.
void mailDone(Session& s) {
    uint8_t slot = slotOf(s);
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
bool mailRead(Session& s, bool quiet) {
    char path[96], buf[80];
    uint32_t nowEpoch = clk::epoch();
    uint8_t slot = mailSlotFor(s.user);
    if (slot == 0xFF) {
        if (!quiet) {
            s.term.color(s.tl, Color::Grey);
            s.term.text(s.tl, "No mail.");
            s.term.nl(s.tl);
        }
        return false;
    }
    if (mailExpired(g_mailAt[slot], nowEpoch)) {                   // gone stale
        mailRewrite(static_cast<int16_t>(slot), nullptr, nowEpoch);
        s.term.color(s.tl, Color::Yellow);
        snprintf(buf, sizeof(buf), "A message for you expired after %u days.",
                 static_cast<unsigned>(g_mailDays));
        s.term.text(s.tl, buf);
        s.term.nl(s.tl);
        return false;
    }
    if (!mailPath(path, sizeof(path))) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    // The slot mailIndex() found, read by position rather than by searching
    // for the handle again: with more than one message waiting those are
    // different records, and taking the first match would read the same one
    // for ever while the rest piled up behind it.
    MailRec r;
    bool found = false;
    int16_t seen = 0;
    while (fread(&r, sizeof(r), 1, f) == 1) {
        if (!r.to[0]) continue;
        if (seen++ == static_cast<int16_t>(slot)) { found = true; break; }
    }
    fclose(f);
    if (!found) return false;

    char when[24] = "";
    if (r.at) clk::fmtEpoch(when, sizeof(when), "%d %b %H:%M", r.at);
    snprintf(buf, sizeof(buf), "Message from %.20s%s%s", r.from, when[0] ? ", " : "", when);
    s.term.color(s.tl, g_cRoom);
    s.term.text(s.tl, buf);
    s.term.nl(s.tl);
    r.text[kMailChars] = '\0';
    s.term.color(s.tl, g_cText);
    s.term.text(s.tl, r.text);
    s.term.nl(s.tl);
    // Nothing has been decided yet, so nothing has been touched. That is
    // the whole difference between a mailbox and a message that evaporates
    // the moment somebody looks at it.
    uint8_t id   = slotOf(s);
    bool    room = Bbs::instance().owns(s, g_index);
    if (!room && !Bbs::instance().own(s, g_index)) {
        // No way to read single keys, so there is no decision to offer.
        // Mark it kept rather than leaving it unread: unread would show
        // this same message again on the next MAIL, for ever.
        mailRewrite(-1, nullptr, nowEpoch, static_cast<int16_t>(slot));
        return true;
    }

    g_mailIdx [id] = static_cast<int16_t>(slot);
    g_mailRoom[id] = room;
    g_mailMode[id] = MM_CHOOSE;
    snprintf(g_mailWho[id], BBS_USER_MAX + 1, "%.*s", BBS_USER_MAX, r.from);
    mailChoosePrompt(s);
    return true;
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
        interrupt(*now, Color::Yellow,
                  joined(*now) ? "You have mail. /e reads it."
                               : "You have mail. MAIL reads it.");
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
    snprintf(row, sizeof(row), "%-10.10s", cmd);
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
void roomHelp(Session& s) {
    wipeInput(s);
    mark(s);
    s.term.color(s.tl, g_cRoom);
    s.term.text(s.tl, "Room Commands");
    s.term.nl(s.tl);
    helpLine(s, "/s", "who is here");
    helpLine(s, "/p n text", "one line to node n");
    helpLine(s, "/me text", "an action line");
    helpLine(s, "/a [note]", "away, or back again");
    helpLine(s, "/sq n", "hide or show a node");
    helpLine(s, "/email h m", "leave a message");
    helpLine(s, "/e", "read yours");
    helpLine(s, "/t", "the time");
    helpLine(s, "/clear", "wipe the screen");
    helpLine(s, "/welcome", "the screen you came in on");
    helpLine(s, "/q", "leave the room");
    if (s.perms) {
        helpLine(s, "/k n [why]", "kick a node out");
        helpLine(s, "/b handle", "bar from the room");
        helpLine(s, "/unb handle", "let them back");
        helpLine(s, "/bans", "who is barred");
    } else {
        helpLine(s, "/vk n", "vote to kick, no staff here");
    }
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
    target.term.eraseBack(target.tl, target.ed.shown());
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
    const char* arg = p;
    while (*arg && *arg != ' ') ++arg;                 // the verb ends here
    size_t vlen = static_cast<size_t>(arg - p);
    while (*arg == ' ') ++arg;

    auto is = [&](const char* word) {
        return strlen(word) == vlen && !strncasecmp(p, word, vlen);
    };

    if (is("/?") || is("/help") || is("/h")) { roomHelp(s); return true; }
    if (is("/q") || is("/quit"))             { leave(s, "left the room"); return true; }
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

    if (is("/t") || is("/time")) {
        char when[24] = "no clock";
        if (clk::valid()) clk::fmt(when, sizeof(when), "%a %d %b %H:%M");
        wipeInput(s);
        tell(s, g_cRoom, when);
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
        flush(s);
        tag(s, me, sizeof(me));
        snprintf(line, sizeof(line), "%.28s * %.48s", me, arg);
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
            to->term.eraseBack(to->tl, to->ed.shown());
            to->term.color(to->tl, g_cPriv);
            to->term.text(to->tl, ">");
            showLine(*to, line, false);
            to->ed.redraw(to->term, to->tl);
            if (g_away[slotOf(*to)][0]) {
                snprintf(buf, sizeof(buf), "%.20s is away: %.16s", to->user, g_away[slotOf(*to)]);
                tell(s, Color::Grey, buf);
            }
            s.term.color(s.tl, g_cPriv);
            snprintf(buf, sizeof(buf), ">to #%s %.40s", nodeName(*to).t, rest);
            s.term.text(s.tl, buf);
            s.term.nl(s.tl);
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
        char buf[80];
        s.term.text(s.tl, "R");
        s.term.nl(s.tl);
        snprintf(buf, sizeof(buf), "Reply to %.20s:", g_mailWho[slot]);
        tell(s, Color::Cyan, buf);
        g_mailMode[slot] = MM_REPLY;
        uint8_t room = static_cast<uint8_t>(s.term.cols() > 4 ? s.term.cols() - 2 : 32);
        s.ed.begin(room < kLineMax ? room : kLineMax, 0);
        s.term.color(s.tl, Color::White);
        s.term.cursor(s.tl, true);
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
void onKey(Session& s, int k, uint32_t now) {
    // A message is in hand. These keys are not chat: they decide what
    // happens to it, and they are read before the editor sees them because
    // the answer is one keypress rather than a line.
    uint8_t slot = slotOf(s);
    if (g_mailMode[slot] == MM_CHOOSE) { mailChoose(s, k);   return; }
    if (g_mailMode[slot] == MM_REPLY)  { mailReplyKey(s, k); return; }

    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Abort) { leave(s, "left the room"); return; }
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
    say(s, p, now);
}

// onLogin: the one line everybody remembers from a BBS
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
          if (!*a) {
              mailRead(s, false);
              if (!mailBusy(s)) b.prompt(s);    // else the R/S/D prompt has the keys
              return;
          }
          char handle[BBS_USER_MAX + 1];
          const char* text = a;
          while (*text && *text != ' ') ++text;
          size_t hlen = static_cast<size_t>(text - a);
          while (*text == ' ') ++text;
          if (!hlen || !*text) {
              s.term.color(s.tl, Color::LightRed);
              s.term.text(s.tl, "MAIL handle your message");
          } else {
              snprintf(handle, sizeof(handle), "%.*s", static_cast<int>(hlen), a);
              mailSend(s, handle, text);
          }
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
};
