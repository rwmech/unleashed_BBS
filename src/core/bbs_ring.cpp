/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_ring.cpp
 * Module:       Core / the sysop page: OPERATOR, /o (1.1.0)
 *
 * Purpose:      A caller rings for the sysop, the way every board had. PAGE
 *                  is caller to caller; this is the one that gets the
 *                  operator's attention wherever they are, and that can be
 *                  answered, declined, or put off.
 *
 *                  The caller types O (and why, if they did not say), and
 *                  waits with a spinner for up to 45 seconds; any key stops
 *                  it. The sysop gets a bell and a flashing RING wherever
 *                  they are, through the bus like a page, and a one-key
 *                  question owned by the core: [A]nswer [D]ecline [X] Away
 *                  [Q] Later. In the chat room there is no question; the room
 *                  says who is ringing and /o or /o- answers. In a form the
 *                  status line says so and O at the prompt asks again.
 *
 *                  Answering puts both of them in the chat room with a
 *                  sticky private already aimed at each other, so what either
 *                  types goes to the other. Anything else leaves a note, shown
 *                  at the sysop's next login or elevation and then cleared.
 *
 * Design:       One ring at a time on the whole board, so the state is one
 *               small struct on Bbs rather than anything per session: which
 *               caller, which sysop session, when it runs out. The caller
 *               and the sysop are named by Session::id and looked up each
 *               time, never held by pointer, because sessions come from a
 *               static pool and moveSession changes an id.
 *
 *               A sysop who is hidden or lurking is answered exactly as one
 *               who is not on at all: the same words, and at once. A hidden
 *               sysop is never rung, so nothing about the wait can give them
 *               away either (PLAN-1.1.0, and the copy's builder notes).
 *
 * Interfaces:   Bbs::cmdOperator, ringAnswer, ringDecline (public); the rest
 *               is the scheduler's and deliverMail's.
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     ring.h, COMMANDS.md (OPERATOR), CHAT.md (/o),
 *               internal/tty-ux-1.1.0-2026-09-23.md section 1,
 *               internal/copy-1.1.0-2026-09-23.md section 1
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

#include "bbs.h"
#include "bbs_util.h"
#include "ring.h"
#include "fx.h"
#include "clock.h"
#include "claims.h"
#include "plugin.h"
#include "../plugins/chat.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bbsu;

namespace {

// The words, from internal/copy-1.1.0-2026-09-23.md section 1, by their ids.
constexpr const char kTag[]         = " RING ";                                        // OP-tag
constexpr const char kQuestion[]    = "[A]nswer [D]ecline [X] Away [Q] Later: ";      // OP-question
constexpr const char kAsk[]         = "What do you need the sysop for?";              // OP-ask
constexpr const char kCancel[]      = "Nothing sent.";                                 // OP-cancel
constexpr const char kRinging[]     = "Ringing the sysop ";                            // OP-ringing
constexpr const char kHint[]        = "   (any key stops)";                            // OP-ringing-hint
constexpr const char kAnswered[]    = "The sysop answered. You're in the chat room, and what "
                                      "you type goes to the sysop only. /q leaves.";  // OP-answered
constexpr const char kDeclined[]    = "The sysop can't talk right now. What you wrote is saved "
                                      "for them.";                                     // OP-declined
constexpr const char kAway[]        = "The sysop is away. What you wrote is saved for them.";          // OP-away
constexpr const char kUnavailable[] = "The sysop isn't available. What you wrote is saved for them.";  // OP-unavailable
constexpr const char kNoAnswer[]    = "No answer. What you wrote is saved for the sysop.";             // OP-noanswer
constexpr const char kStopped[]     = "You stopped ringing. What you wrote is saved for the sysop.";   // OP-stopped
constexpr const char kRateCall[]    = "Three rings is the most for one call.";        // OP-rate-call
constexpr const char kRateBoard[]   = "One ring at a time. Try in a minute.";         // OP-rate-board
constexpr const char kDidDecline[]  = "Declined. They have been told.";               // OP-did-decline
constexpr const char kDidAway[]     = "You're away: rings are saved as notes and pages are off. "
                                      "DND brings you back.";                          // OP-did-away
constexpr const char kDidLater[]    = "Still ringing. O answers while it does.";      // OP-did-later
constexpr const char kNone[]        = "Nobody is ringing.";                            // OP-none
// Not in the copy (1.1.0, Rob: missed rings go to MAIL): what a bare O says
// after OP-none, so the sysop knows where the rings they missed are.
constexpr const char kToMail[]      = "Missed rings go to MAIL.";
constexpr const char kRoom2[]       = "/o answers, /o- declines.";                     // OP-room-2
constexpr const char kOver[]        = "Only the last 8 are kept.";                     // OP-login-over
constexpr const char kGone[]        = "Shown once. They are cleared now.";             // OP-login-gone
// Not in the copy: the one case it did not cover, a board with the chat
// room switched off, where answering has nowhere to put the two of them.
// The ring keeps ringing, so a PAGE or a decline is still open to them.
constexpr const char kNoRoom[]      = "No chat room here to talk in. PAGE them, or D.";

constexpr uint8_t    kHintCols      = sizeof(kHint) - 1;
constexpr const char kNotesFile[]   = "rings.txt";

// Keys in the first half second of a ring are not "any key". A plain ASCII
// terminal with no telnet sends CR LF for Enter, and the LF would otherwise
// stop the ring the instant it started.
constexpr uint32_t   kRingGraceMs   = 500;

// chatOwned: the chat plugin has this session: the room, or the mailbox.
// Both mark the board's own lines with -->, so a ring's lines there do too;
// a room has no prompt character, and without the marker "No answer." reads
// like somebody saying it.
bool chatOwned(const Session& s) {
    return s.owner != 0xFF && s.owner == plugins::indexOf("chat");
}

// sayWrapped: text from column 0, word wrapped at the reader's row width, in
// one colour, every line ended. The house rule for a line the board says
// (Rob: "--> starts at the very begining. EVERYWHERE").
void sayWrapped(Session& s, Color c, const char* text) {
    if (chatOwned(s)) { chat::roomSay(s, c, text); return; }
    char line[160];
    const char* p = text;
    uint8_t w = Bbs::instance().rowWidth(s);
    uint8_t guard = 0;
    s.term.color(s.tl, c);
    while ((p = wrap(p, line, sizeof(line), w)) != nullptr && ++guard < 8) {
        s.term.text(s.tl, line);
        s.term.nl(s.tl);
        if (!*p) break;
    }
}

// sayLine: one short line, in one colour, ended.
void sayLine(Session& s, Color c, const char* text) {
    sayWrapped(s, c, text);
}

// ringerLine: "quantumrob (3) is ringing: can't upload to Drop Box", or
// "(3, guest)" for a guest: the sysop is deciding whether to answer, and "no
// account to reply to" is the fact that matters (OP-notice, OP-notice-guest).
void ringerLine(char* out, size_t n, const char* handle, uint8_t node, bool guest,
                const char* reason) {
    snprintf(out, n, guest ? "%s (%s, guest) is ringing: %s" : "%s (%s) is ringing: %s",
             handle, nodeNum(node).t, reason);
}

void notesPath(char* out, size_t n) {
    snprintf(out, n, "%s/%s", plat::userBase(), kNotesFile);
}

} // namespace

// ===========================================================================
// Who
// ===========================================================================

Session* Bbs::sessionById(uint8_t id) {
    if (id == 0) return &sysop_;
    if (id >= 1 && id <= BBS_MAX_NODES) return &nodes_[id - 1];
    if (id == BBS_MAX_NODES + 1) return &busy_;
    return nullptr;
}

// ---------------------------------------------------------------------------
// ringTarget: the sysop session to ring, or null when there is none to ask.
//
// Any session holding the sysop level counts: the sysop node, or a second
// sysop session on a caller line (elevate() gives one to a local caller when
// the node is taken). Hidden or lurking is as good as not on. DND is on and
// visible, which is "away" and said so: away is set when that is the only
// reason nobody can be asked.
//
// Only a session a ring can actually reach is asked. At the prompt, in a
// plugin that can lift its input, in a form or the user manager, at [More]
// or "Log off (Y/N)?" it shows at once; a list still being drawn gets there
// in a moment, and a refreshing WHO n or DASH n is stopped for it. SNOOP,
// the FX demo, a paused screen or a plugin with no hooks (the serial bridge)
// would hold it the whole 45 seconds and answer "No answer" for a sysop who
// never saw it, so they count as not there. Among those that can be asked,
// one that sees it now wins over one that sees it soon: a sysop with DASH in
// one window and a prompt in the other is rung at the prompt. The sysop node
// first within each.
// ---------------------------------------------------------------------------
Session* Bbs::ringTarget(bool& away) {
    away = false;
    for (uint8_t pass = 0; pass < 2; ++pass) {
        for (uint8_t i = 0; i <= BBS_MAX_NODES; ++i) {
            Session* o = i == 0 ? &sysop_ : &nodes_[i - 1];
            if (o->st == SState::Free || !o->loggedIn || o->fd < 0) continue;
            if (o->level != Access::Sysop) continue;
            if (!o->visible || o->lurk) continue;        // hidden: as good as not on
            if (o->dnd) { away = true; continue; }
            bool now = o->st == SState::Shell || o->st == SState::Form ||
                       o->st == SState::UserList || o->st == SState::More ||
                       o->st == SState::Confirm;
            if (o->st == SState::Plugin && o->owner != 0xFF && plugins::running(o->owner)) {
                const Plugin* p = plugins::at(o->owner);
                now = p && p->liftInput && p->restoreInput;
            }
            bool soon = o->st == SState::List || o->st == SState::Watch;
            if (pass == 0 ? now : soon) { away = false; return o; }
        }
    }
    return nullptr;
}

// ringLive: this queued Ring is the ring still running, and this is the
// session it rings.
bool Bbs::ringLive(const Session& s, const BusMsg& m) const {
    if (ring_.from == 0xFF || ring_.to != s.id) return false;
    return strtoul(m.text, nullptr, 10) == ring_.seq;
}

// ===========================================================================
// The caller
// ===========================================================================

// ringRefused: which rule said no, in LightRed, in place of the question.
void Bbs::ringRefused(Session& s, uint8_t verdict, uint8_t minutes) {
    char buf[48];
    const char* msg = kRateBoard;
    if (verdict == static_cast<uint8_t>(ring::Verdict::Call)) {
        msg = kRateCall;
    } else if (verdict == static_cast<uint8_t>(ring::Verdict::Wait)) {
        // OP-rate-2 and OP-rate-1, and 3 straight after a ring: the gap is
        // counted from when the last one started and rounded up.
        snprintf(buf, sizeof(buf), "Already rung. Try again in %u minute%s.",
                 static_cast<unsigned>(minutes), minutes == 1 ? "" : "s");
        msg = buf;
    }
    sayLine(s, Color::LightRed, msg);
}

// ---------------------------------------------------------------------------
// cmdOperator: OPERATOR, O, and the room's /o for anybody but the sysop.
//
// For the sysop it is the other end: a bare O asks again about a ring that
// is still waiting (they pressed Q, or it reached them in a form). With no
// ring live it shows the notes rings have left (1.1.0), which the sysop
// could otherwise read only at the next login or elevation, however plainly
// the dashboard said they were there; and "Nobody is ringing." when there
// are none. Draws no prompt and re-arms nothing; the shell's table and the
// room each finish in their own way, and every line this prints is ended,
// so either can.
// ---------------------------------------------------------------------------
void Bbs::cmdOperator(Session& s, const char* arg) {
    uint32_t now = plat::millis();
    if (s.level == Access::Sysop) {
        if (ring_.from != 0xFF && ring_.to == s.id) { ringAsk(s, false); return; }
        if (ringNotes(s)) return;
        // Notes still counted were kept for want of room on the line, and
        // "Nobody is ringing." would be the wrong sentence: the next O shows
        // them. Reached only with output still queued, which a bare O at
        // the prompt all but never has.
        if (ringNotesWaiting_) return;
        sayLine(s, Color::Grey, kNone);
        // And where the ones that were missed went, when that is MAIL.
        uint8_t room = plugins::indexOf("chat");
        if (room != 0xFF && plugins::running(room) && chat::mailOn())
            sayLine(s, Color::Grey, kToMail);
        return;
    }

    uint8_t minutes = 0;
    ring::Verdict v = ringLimits_.check(s.id, now, ring_.from != 0xFF, minutes);
    if (v != ring::Verdict::Ok) {
        ringRefused(s, static_cast<uint8_t>(v), minutes);
        return;
    }

    while (*arg == ' ') ++arg;
    if (!*arg) {
        // A ring with no reason is a bell the sysop learns to ignore
        // (GTalk's rule), so a bare O asks for one, and nothing typed is
        // nothing sent.
        sayLine(s, Color::Cyan, kAsk);
        s.term.color(s.tl, Color::White);
        s.ed.begin(ring::kReasonMax, 0);
        s.st = SState::RingWhy;
        return;
    }
    ringStart(s, arg, now);
}

// ringWhyKey: the reason being typed. Enter sends it, ESC or nothing does not.
void Bbs::ringWhyKey(Session& s, int k, uint32_t now) {
    if (!s.tl.empty()) s.tl.skipDelays();
    LineEditor::Res r = s.ed.key(k, s.term, s.tl);
    if (r == LineEditor::Res::Editing) return;

    char why[ring::kReasonMax + 1];
    snprintf(why, sizeof(why), "%s", r == LineEditor::Res::Done ? s.ed.text() : "");
    const char* p = why;
    while (*p == ' ') ++p;
    if (!*p) {
        sayLine(s, Color::Grey, kCancel);
        ringBack(s);
        return;
    }
    // Asked again: while they were typing somebody else may have started
    // ringing, and a second ring on top of the first is exactly what the
    // board-wide rule is there to stop.
    uint8_t minutes = 0;
    ring::Verdict v = ringLimits_.check(s.id, now, ring_.from != 0xFF, minutes);
    if (v != ring::Verdict::Ok) {
        ringRefused(s, static_cast<uint8_t>(v), minutes);
        ringBack(s);
        return;
    }
    if (!ringStart(s, p, now)) ringBack(s);
}

// ---------------------------------------------------------------------------
// ringStart: the reason is in; ring, or say at once why nobody will answer.
// True when a ring is running and the caller is waiting on it.
//
// Not on, hidden and lurking all get OP-unavailable, straight away: one
// sentence and one timing for all three, so HIDE stays undetectable. Ringing
// a hidden sysop for 45 seconds while answering an absent one at once would
// give HIDE away however the words read.
// ---------------------------------------------------------------------------
bool Bbs::ringStart(Session& s, const char* reason, uint32_t now) {
    ringLimits_.spend(s.id, now);

    bool away = false;
    Session* sys = ringTarget(away);
    if (!sys) {
        ring::Note n;
        n.epoch = clk::epoch();
        n.node  = s.id;
        n.guest = s.guest;
        snprintf(n.handle, sizeof(n.handle), "%s", s.user);
        ring::cleanCopy(n.reason, sizeof(n.reason), reason);
        bool mailed = ringLeave(n);
        plat::log("bbs: node %u rang for the sysop, %s: %s", s.id,
                  away ? "away" : "not available", mailed ? "mailed" : "note left");
        sayWrapped(s, Color::Yellow, away ? kAway : kUnavailable);
        return false;
    }

    ++ring_.seq;
    if (!ring_.seq) ring_.seq = 1;
    ring_.from    = s.id;
    ring_.to      = sys->id;
    ring_.guest   = s.guest;
    ring_.shown   = false;
    ring_.spin    = 0;
    ring_.started = now;
    ring_.ends    = now + ring::ringMs();
    ring_.spinAt  = now + ring::kSpinMs;
    snprintf(ring_.handle, sizeof(ring_.handle), "%s", s.user);
    ring::cleanCopy(ring_.reason, sizeof(ring_.reason), reason);

    char seq[12];
    snprintf(seq, sizeof(seq), "%lu", static_cast<unsigned long>(ring_.seq));
    post(*sys, BusKind::Ring, &s, seq);

    // "Ringing the sysop /   (any key stops)", with the cursor parked just
    // after the spinner: each frame is one step back and one character, and
    // the hint stays where it is. Three cursor moves any terminal has.
    Term& t = s.term;
    t.cursor(s.tl, false);
    t.color(s.tl, Color::Grey);
    t.text(s.tl, kRinging);
    t.color(s.tl, Color::Yellow);
    fx::spinFrame(t, s.tl, fx::Spin::Line, 0);
    t.color(s.tl, Color::DarkGrey);
    t.text(s.tl, kHint);
    t.left(s.tl, kHintCols);
    s.st = SState::Ringing;
    plat::log("bbs: node %u rings for the sysop on node %s", s.id, nodeName(*sys).t);
    return true;
}

// ringStopSpin: take the spinner and the hint off the caller's line and end
// it, leaving "Ringing the sysop" where it was.
void Bbs::ringStopSpin(Session& c) {
    Term& t = c.term;
    t.reset(c.tl);
    t.left(c.tl, 1);
    t.eraseEol(c.tl, static_cast<uint8_t>(1 + kHintCols));
    // eraseEol writes spaces where the terminal has no erase of its own,
    // which moves the cursor. Back to where the spinner was, so the line
    // ends there rather than past a row of blanks.
    if (!t.isAnsi()) t.left(c.tl, static_cast<uint8_t>(1 + kHintCols));
    t.cursor(c.tl, true);
    t.nl(c.tl);
}

// ---------------------------------------------------------------------------
// ringBack: hand a session back to wherever it was when the core took it,
// the plugin that owns it through its restoreInput, or the shell prompt with
// whatever was typed on it.
// ---------------------------------------------------------------------------
void Bbs::ringBack(Session& s) {
    if (s.owner != 0xFF) {
        s.st = SState::Plugin;
        const Plugin* p = plugins::running(s.owner) ? plugins::at(s.owner) : nullptr;
        if (p && p->restoreInput) { p->restoreInput(s); return; }
        release(s);                              // draws the prompt
        return;
    }
    if (s.ed.active()) {                         // the sysop, rung at the prompt
        s.st = SState::Shell;
        drawPrompt(s);
        s.ed.redraw(s.term, s.tl);
        return;
    }
    prompt(s);
}

// ===========================================================================
// The sysop
// ===========================================================================

// ringNotice: who is ringing and why, with the bell and the flashing tag the
// first time and without them when O asks again.
void Bbs::ringNotice(Session& s, bool fanfare) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    t.reset(tl);
    if (fanfare) {
        if (!s.bellOff) t.bell(tl);
        t.color(tl, Color::LightRed);
        fx::blink(t, tl, kTag, 3, 150);
        fx::pause(tl, 250);
        fx::rubout(t, tl, static_cast<uint8_t>(sizeof(kTag) - 1), 25);
    }
    char line[128];
    ringerLine(line, sizeof(line), ring_.handle, ring_.from, ring_.guest, ring_.reason);
    sayWrapped(s, Color::Yellow, line);
}

// ringAsk: the notice and the one-key question, owned by the core. The
// session goes back to the shell or its plugin once it is answered.
void Bbs::ringAsk(Session& s, bool fanfare) {
    ringNotice(s, fanfare);
    s.term.color(s.tl, Color::Cyan);
    // 39 with the space the cursor sits after, which at 40 columns puts the
    // echoed key in the last column, where a C64 wraps before the newline
    // and leaves a blank line. The copy's own fallback: the 38 of the UX
    // spec, without the space, on a 40 column screen.
    s.term.textN(s.tl, kQuestion, sizeof(kQuestion) - (s.term.cols() > 40 ? 1 : 2));
    s.term.color(s.tl, Color::White);
    s.term.cursor(s.tl, true);
    s.st = SState::RingAsk;
    ring_.shown = true;
}

// ringShow: a Ring off the bus, to a session that can take it now. In the
// room it is two lines of the room's own voice and nothing to answer, since
// /o and /o- are commands there; anywhere else the question. True when the
// question is up, and then the caller must not put the input back: that
// happens once it is answered.
bool Bbs::ringShow(Session& s, bool room) {
    if (room) {
        char line[128];
        ringerLine(line, sizeof(line), ring_.handle, ring_.from, ring_.guest, ring_.reason);
        if (!s.bellOff) s.term.bell(s.tl);
        chat::roomSay(s, Color::Yellow, line);        // OP-room-1
        chat::roomSay(s, Color::Grey, kRoom2);        // OP-room-2
        ring_.shown = true;
        return false;
    }
    ringAsk(s, true);
    return true;
}

// ringFormHint: a ring reaching somebody in a form, on its status line. The
// handle when it fits in the 38 columns, the node alone when it does not:
// a cut handle names somebody else (OP-form, OP-form-short).
void Bbs::ringFormHint(Session& s) {
    char line[64];
    snprintf(line, sizeof(line), "RING %s (%s). ESC, then O.", ring_.handle, nodeNum(ring_.from).t);
    if (strlen(line) > 38)
        snprintf(line, sizeof(line), "RING from node %s. ESC, then O.", nodeNum(ring_.from).t);
    if (!s.bellOff) s.term.bell(s.tl);
    warnNow(s, line);
    ring_.shown = true;
}

// ringAskKey: the answer to [A]nswer [D]ecline [X] Away [Q] Later.
void Bbs::ringAskKey(Session& s, int k, uint32_t now) {
    (void)now;
    // A key that arrived while the notice was still being drawn was typed
    // before the question was on the screen, so it is not an answer to it:
    // a sysop typing a command when the ring landed would otherwise answer
    // or decline it with whatever letter came next. Dropped, deliberately.
    if (!s.tl.empty()) return;
    Term& t = s.term;
    Timeline& tl = s.tl;
    if (ring_.from == 0xFF || ring_.to != s.id) {   // over while it was asked
        t.nl(tl);
        ringBack(s);
        return;
    }
    switch (k) {
        case 'a': case 'A':
            t.ch(tl, 'A');
            t.nl(tl);
            if (!ringAnswer(s)) ringBack(s);
            return;
        case 'd': case 'D':
            t.ch(tl, 'D');
            t.nl(tl);
            ringDecline(s);
            ringBack(s);
            return;
        case 'x': case 'X':
            t.ch(tl, 'X');
            t.nl(tl);
            s.dnd = true;                          // X is the existing DND
            ringEnd(RingEnd::Away);
            sayWrapped(s, Color::Yellow, kDidAway);
            ringBack(s);
            return;
        default:
            // Q, and anything else: the question goes, the ring does not.
            // The key is spent on this rather than passed on, because it
            // was pressed at a question.
            if (k == 'q' || k == 'Q') t.ch(tl, 'Q');
            t.nl(tl);
            sayLine(s, Color::Yellow, kDidLater);
            ringBack(s);
            return;
    }
}

// ---------------------------------------------------------------------------
// ringAnswer: A at the question, or /o in the room. Both of them go into the
// chat room with a sticky private aimed at the other, the caller first,
// because they have been waiting. False, having said why, when there was
// nothing to answer or nowhere to talk.
// ---------------------------------------------------------------------------
bool Bbs::ringAnswer(Session& s) {
    if (ring_.from == 0xFF || ring_.to != s.id) {
        sayLine(s, Color::Grey, kNone);
        return false;
    }
    uint8_t room = plugins::indexOf("chat");
    Session* c = sessionById(ring_.from);
    if (room == 0xFF || !plugins::running(room)) {
        sayLine(s, Color::LightRed, kNoRoom);
        return false;
    }
    if (!c || c->st != SState::Ringing) {
        sayLine(s, Color::Grey, kNone);
        return false;
    }

    char handle[BBS_USER_MAX + 1];
    snprintf(handle, sizeof(handle), "%s", ring_.handle);
    uint8_t cid = c->id, sid = s.id;
    ringClear();                                   // answered: no note

    ringStopSpin(*c);
    c->st = c->owner != 0xFF ? SState::Plugin : SState::Shell;
    if (!chat::converse(*c, sid, kAnswered)) ringBack(*c);

    // Out of wherever the sysop was. A plugin that is not the room loses the
    // session the way a dropped line would lose it; a page or a post being
    // written there is not kept, which is what answering means.
    if (s.owner != 0xFF && s.owner != room) ringYank(s);
    s.st = s.owner != 0xFF ? SState::Plugin : SState::Shell;
    char said[112];
    snprintf(said, sizeof(said), "Answered. What you type goes to %s only. /q leaves the room.",
             handle);                              // OP-did-answer
    if (!chat::converse(s, cid, said)) ringBack(s);
    plat::log("bbs: node %s answered the ring from node %u", nodeName(s).t, static_cast<unsigned>(cid));
    return true;
}

// ringDecline: D at the question, or /o- in the room.
bool Bbs::ringDecline(Session& s) {
    if (ring_.from == 0xFF || ring_.to != s.id) {
        sayLine(s, Color::Grey, kNone);
        return false;
    }
    ringEnd(RingEnd::Declined);
    sayLine(s, Color::Yellow, kDidDecline);
    return true;
}

// ringYank: take a session out of the plugin that owns it, without that
// plugin's help. What it held (a page being written, the forums' subject
// table) is let go the same way a hangup lets it go, and release() draws no
// prompt because the session is not SState::Plugin by then.
void Bbs::ringYank(Session& s) {
    if (s.owner == 0xFF) return;
    claims::releaseAll(s.id);
    s.st = SState::Shell;
    release(s);
}

// ===========================================================================
// The end of a ring
// ===========================================================================

// ringClear: no ring is running. The notice is taken back out of the
// sysop's queue too, so a Ring still waiting there is always a live one.
void Bbs::ringClear() {
    Session* sys = sessionById(ring_.to);
    ring_.from = ring_.to = 0xFF;
    if (sys) {
        BusMsg gone;
        while (sys->mb.take(Mailbox::bit(BusKind::Ring), gone)) {}
    }
}

// ---------------------------------------------------------------------------
// ringEnd: every way a ring can end except being answered. The caller is
// told (unless they hung up), the note is written, and the sysop is told if
// the caller went away while it rang, closing the question if it was up.
// ---------------------------------------------------------------------------
void Bbs::ringEnd(RingEnd how) {
    if (ring_.from == 0xFF) return;
    Session* c   = sessionById(ring_.from);
    Session* sys = sessionById(ring_.to);

    ring::Note n;
    n.epoch = clk::epoch();
    n.node  = ring_.from;
    n.guest = ring_.guest;
    snprintf(n.handle, sizeof(n.handle), "%s", ring_.handle);
    snprintf(n.reason, sizeof(n.reason), "%s", ring_.reason);
    ringClear();                                   // over, before anything below looks
    bool mailed = ringLeave(n);

    if (c && c->st == SState::Ringing && how != RingEnd::HungUp) {
        const char* say = kNoAnswer;
        switch (how) {
            case RingEnd::Declined: say = kDeclined;    break;
            case RingEnd::Away:     say = kAway;        break;
            case RingEnd::Stopped:  say = kStopped;     break;
            case RingEnd::Gone:     say = kUnavailable; break;
            default:                                    break;
        }
        ringStopSpin(*c);
        sayWrapped(*c, Color::Yellow, say);
        ringBack(*c);
    }

    // OP-ended and OP-hungup: the caller went away without the sysop. Not
    // for Gone, which is the sysop going away, and not for the sysop's own
    // D or X, which the sysop has just done.
    bool callerLeft = how == RingEnd::NoAnswer || how == RingEnd::Stopped ||
                      how == RingEnd::HungUp;
    if (callerLeft && sys && sys->st != SState::Free && sys->loggedIn && sys->fd >= 0) {
        char line[96];
        snprintf(line, sizeof(line), mailed ? "%s (%s) %s. It is in MAIL."
                                            : "%s (%s) %s. Their note is saved.", n.handle,
                 nodeNum(n.node).t, how == RingEnd::HungUp ? "hung up" : "stopped ringing");
        if (sys->st == SState::RingAsk) {          // the question is on their screen
            sys->term.nl(sys->tl);
            sayWrapped(*sys, Color::Yellow, line);
            ringBack(*sys);
        } else {
            notify(*sys, line);
        }
    }
    static const char* const kHow[] = { "answered", "declined", "away", "no answer",
                                        "stopped", "hung up", "sysop gone" };
    plat::log("bbs: ring from node %u ended: %s, %s", static_cast<unsigned>(n.node),
              kHow[static_cast<uint8_t>(how)], mailed ? "mailed" : "note left");
}

// ringClosed: a line is closing. A caller who rang hung up; a sysop who was
// rung has gone. Called from closeSession before the session is torn down.
void Bbs::ringClosed(Session& s) {
    if (ring_.from == 0xFF) return;
    if (s.id == ring_.from)    ringEnd(RingEnd::HungUp);
    else if (s.id == ring_.to) ringEnd(RingEnd::Gone);
}

// ---------------------------------------------------------------------------
// serviceRing: once a pass. The spinner, the 45 seconds, and a ring whose
// caller or sysop has quietly stopped being one (a hangup on its way out, a
// sysop who typed DROP after Q).
// ---------------------------------------------------------------------------
void Bbs::serviceRing(uint32_t now) {
    if (ring_.from == 0xFF) return;
    Session* c   = sessionById(ring_.from);
    Session* sys = sessionById(ring_.to);
    if (!c || c->st != SState::Ringing) { ringEnd(RingEnd::HungUp); return; }
    // Gone includes going hidden: HIDE or LURK after Q. A hidden sysop is
    // never rung, and one left ringing would also hold the board-wide "one
    // at a time" against the next caller, which says a sysop is there.
    if (!sys || sys->st == SState::Free || sys->st == SState::Closing || !sys->loggedIn ||
        sys->fd < 0 || sys->level != Access::Sysop || !sys->visible || sys->lurk) {
        ringEnd(RingEnd::Gone);
        return;
    }
    if (sys->dnd) { ringEnd(RingEnd::Away); return; }   // DND typed after Q
    if (static_cast<int32_t>(now - ring_.ends) >= 0) { ringEnd(RingEnd::NoAnswer); return; }
    // A frame only onto an empty line out: a slow terminal spins slower
    // rather than falling behind a queue of frames it will never catch up.
    if (static_cast<int32_t>(now - ring_.spinAt) >= 0 && c->tl.empty()) {
        c->term.left(c->tl, 1);
        c->term.color(c->tl, Color::Yellow);
        fx::spinFrame(c->term, c->tl, fx::Spin::Line, ++ring_.spin);
        ring_.spinAt = now + ring::kSpinMs;
    }
}

// ringingKey: any key stops a ring, after the first half second.
void Bbs::ringingKey(Session& s, uint32_t now) {
    if (ring_.from != s.id) return;
    if (now - ring_.started < kRingGraceMs) return;
    ringEnd(RingEnd::Stopped);
}

// ===========================================================================
// Where a missed ring goes: MAIL, or a note
// ===========================================================================

// ---------------------------------------------------------------------------
// ringLeave: see bbs.h. Rob, 1.1.0: "the sysop page should drop to email".
//
// Mail rather than a file of its own, so a missed ring is in the one place
// the sysop already reads: "You have mail" at login, the dashboard's waiting
// row, and the display's letter icon all count it without knowing rings
// exist. Stored through the chat plugin's own writer (chat::leaveMail), so
// the box limits and the notice to a sysop who is on are MAIL's own.
//
// "The sysop" for mail is one account (Rob, 1.1.0): the one CONFIG board
// names, or the last to elevate to sysop (sysopAccount). It was every
// account the sysop password had ever marked, and marks are never taken
// off, so each ring cost a board with several such accounts several of its
// 64 mail slots. The note file stays as the fallback, for a board with no
// such account yet, mail switched off, or the box full, because a ring that
// reached nobody and was then lost would be worse than either.
//
// The message: from the caller's handle, with a guest's marked * as it is
// in every list (and said again in the text, since a guest has no account
// to reply to), "Ring: <reason>" first, then where and when.
// ---------------------------------------------------------------------------
bool Bbs::ringLeave(const ring::Note& n) {
    bool mailed = false;
    uint8_t room = plugins::indexOf("chat");
    if (room != 0xFF && plugins::running(room) && chat::mailOn()) {
        UserRec to;
        if (sysopAccount(to)) {
            char from[BBS_USER_MAX + 1];
            char text[ring::kReasonMax + 96];
            if (n.guest) snprintf(from, sizeof(from), "%.*s*", BBS_USER_MAX - 1, n.handle);
            else         snprintf(from, sizeof(from), "%s", n.handle);
            char when[16] = "";
            if (n.epoch) clk::fmtEpoch(when, sizeof(when), "%H:%M", n.epoch);
            snprintf(text, sizeof(text), "Ring: %s\nRang from node %s%s%s%s.", n.reason,
                     nodeNum(n.node).t, when[0] ? " at " : "", when, n.guest ? ", as a guest" : "");
            mailed = chat::leaveMail(from, to, text);
        }
    }
    if (!mailed) ringSaveNote(n);
    return mailed;
}

// sysopMail: see bbs.h
bool Bbs::sysopMail() const {
    return chat::sysopUnread();
}

// ===========================================================================
// Notes
// ===========================================================================

// ---------------------------------------------------------------------------
// ringSaveNote: add one, keeping the newest kNotesMax, through a temp file and
// a rename. userdata is LittleFS, where rename replaces the old file in one
// step (lfs_rename), so a power cut leaves the old notes or the new ones and
// never neither. Two passes over a file of at most nine lines: one to count,
// one to copy, so no note is ever held in RAM.
// ---------------------------------------------------------------------------
void Bbs::ringSaveNote(const ring::Note& n) {
    char path[96], tmp[104], line[160];
    notesPath(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    uint16_t total = 0;
    uint8_t  have  = 0;
    ring::Note x;
    if (FILE* in = fopen(path, "r")) {
        while (fgets(line, sizeof(line), in)) {
            if (ring::parseHeader(line, total)) continue;
            if (ring::parseNote(line, x) && have < 255) ++have;
        }
        fclose(in);
    }
    if (total < have) total = have;               // a header lost to a power cut

    FILE* out = fopen(tmp, "w");
    if (!out) {
        plat::log("bbs: ring note not saved, cannot write %s", tmp);
        return;
    }
    ring::formatHeader(static_cast<uint16_t>(total < 65535 ? total + 1 : total), line, sizeof(line));
    bool ok = fputs(line, out) >= 0;
    uint8_t skip = have >= ring::kNotesMax ? static_cast<uint8_t>(have - (ring::kNotesMax - 1)) : 0;
    if (FILE* in = fopen(path, "r")) {
        while (ok && fgets(line, sizeof(line), in)) {
            if (!ring::parseNote(line, x)) continue;
            if (skip) { --skip; continue; }        // the oldest go first
            ok = fputs(line, out) >= 0;
        }
        fclose(in);
    }
    ring::formatNote(n, line, sizeof(line));
    ok = ok && fputs(line, out) >= 0;
    ok = (fclose(out) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        plat::log("bbs: ring note not saved");
        return;
    }
    // What ringNotes will say "N rings while you were off" about, kept for
    // the dashboard's "Waiting on you" row.
    ringNotesWaiting_ = static_cast<uint16_t>(total < 65535 ? total + 1 : total);
}

// ---------------------------------------------------------------------------
// ringNoteCount: the notes on file, as ringNotes would count them. Once, at
// boot, so the dashboard knows about notes left from before a restart.
// ---------------------------------------------------------------------------
uint16_t Bbs::ringNoteCount() {
    char path[96], line[160];
    notesPath(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    uint16_t total = 0;
    uint16_t count = 0;
    ring::Note n;
    while (fgets(line, sizeof(line), f)) {
        if (ring::parseHeader(line, total)) continue;
        if (ring::parseNote(line, n) && count < 65535) ++count;
    }
    fclose(f);
    if (!count) return 0;
    return total > count ? total : count;
}

// ---------------------------------------------------------------------------
// ringNotes: the notes, to the sysop arriving, once, and then gone.
//
//   2 rings while you were off:
//   22 Sep 22:14 quantumrob (3): can't
//   upload to Drop Box
//   Shown once. They are cleared now.
// ---------------------------------------------------------------------------
bool Bbs::ringNotes(Session& s) {
    char path[96], line[160];
    notesPath(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) { ringNotesWaiting_ = 0; return false; }

    uint16_t total = 0;
    uint8_t  count = 0;
    ring::Note n;
    while (fgets(line, sizeof(line), f)) {
        if (ring::parseHeader(line, total)) continue;
        if (ring::parseNote(line, n) && count < 255) ++count;
    }
    if (!count) {
        fclose(f);
        remove(path);
        ringNotesWaiting_ = 0;
        return false;
    }
    if (total < count) total = count;
    // Eight notes wrapped at 40 columns, with their colours, are about 1.3 KB,
    // and the file is removed once they are shown. A timeline put is all or
    // nothing, and it is silent when it is nothing, so with less room than
    // that they are left for the next login rather than printed into a
    // buffer that drops them and then deleted.
    constexpr size_t kNotesRoom = 1400;
    if (s.tl.freeBytes() < kNotesRoom) {
        fclose(f);
        plat::log("bbs: ring notes kept for later, %u bytes free on node %s",
                  static_cast<unsigned>(s.tl.freeBytes()), nodeName(s).t);
        return false;
    }

    Term& t = s.term;
    Timeline& tl = s.tl;
    char head[48];
    if (total == 1) snprintf(head, sizeof(head), "1 ring while you were off:");
    else            snprintf(head, sizeof(head), "%u rings while you were off:",
                             static_cast<unsigned>(total));
    t.reset(tl);
    sayLine(s, Color::Yellow, head);

    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        if (!ring::parseNote(line, n)) continue;
        char when[24] = "";
        if (n.epoch) clk::fmtEpoch(when, sizeof(when), "%d %b %H:%M", n.epoch);
        char row[160];
        snprintf(row, sizeof(row), n.guest ? "%s%s%s (%s, guest): %s" : "%s%s%s (%s): %s",
                 when, when[0] ? " " : "", n.handle, nodeNum(n.node).t, n.reason);
        sayWrapped(s, Color::Yellow, row);
    }
    fclose(f);
    if (total > count) sayLine(s, Color::Grey, kOver);
    sayLine(s, Color::Grey, kGone);
    t.nl(tl);
    remove(path);
    ringNotesWaiting_ = 0;
    plat::log("bbs: %u ring note%s shown to %s", static_cast<unsigned>(count),
              count == 1 ? "" : "s", s.user);
    return true;
}
