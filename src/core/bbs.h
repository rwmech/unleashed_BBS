/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs.h
 * Module:       Core / sessions and scheduler
 *
 * Purpose:      BBS core: one listener on the dial-in port, a fixed pool of
 *                  caller nodes, a busy line session, a hidden sysop node, and
 *                  a single cooperative loop. Sessions are preallocated in
 *                  static storage; the loop never allocates.
 *
 *                  Commands live in a registry (Command tables): the core table is in
 *                  bbs_shell.cpp, plugins add theirs with registerCommands(). Dispatch,
 *                  permission checks and HELP all come from the tables.
 *
 *                  Sources: bbs.cpp (connections, flow, input, paging, refresh screens),
 *                  bbs_shell.cpp (command table, caller commands, HELP/WHO/DASH),
 *                  bbs_sysop.cpp (sysop node, co-sysops, staff commands).
 *
 * Interfaces:   Bbs (begin, tick, onKey, registerCommands), Session, Command, CmdFlag
 *
 * Libraries:    BSD sockets (lwIP on ESP32)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md
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
#include "../config.h"
#include "term.h"
#include "timeline.h"
#include "telnet.h"
#include "detect.h"
#include "editor.h"
#include "screens.h"
#include "bus.h"
#include "guard.h"
#include "sysconfig.h"
#include "backup.h"
#include "cardnames.h"
#include "users.h"
#include "form.h"
#include "ring.h"
#include "../platform/platform.h"

enum class SState : uint8_t {
    Free,      // slot unused
    Detect,    // terminal detection
    Intro,     // welcome screens playing
    AskName,   // handle prompt
    AskPass,   // password for an existing account
    AskRegister, // unknown handle: register (Y/n)?
    AskKnowMore, // sign-up: "would you like to know more?" before the form
    AnyKey,      // something was printed worth reading: waiting for a key
    Form,      // a fill-in form (signup, profile, password, user add/edit)
    UserList,  // staff user manager screen
    Shell,     // command prompt, or a screen playing inside the shell
    List,      // a paged list (WHO, LAST, NODES...) is being generated
    More,      // "[More]" prompt waiting for a key
    Confirm,   // "Log off (Y/N)?"
    Fx,        // effects demo running
    Watch,     // WHO n / DASH n refreshing until a key
    Plugin,    // a plugin owns the session (serial bridge, chat, a door)
    BusyWait,  // busy line countdown
    Snoop,     // sysop watching another node
    Approve,   // sysop answering Y/N on a staged backup upload
    Closing,   // flushing goodbye, then hang up
    AskSetup,  // an unconfigured board asks a local caller for the sysop password
    // OPERATOR (1.1.0). All three are the core's even when a plugin owns the
    // session (a caller ringing from the chat room, a sysop answering in the
    // forums): Session::owner stays set, and the session goes back to it,
    // through its restoreInput, when the ring is dealt with.
    RingWhy,   // a caller typing what they want the sysop for
    Ringing,   // a caller waiting on a ring: the spinner, any key stops it
    RingAsk,   // the sysop's one-key question: answer, decline, away, later
    CardJob,   // BACKUP SD / RESTORE SD working, or asking Y/N (1.1.0)
    AskSysop,  // the sysop's account at login: "Sysop password:", Enter skips (1.1.0)
    Waiting,   // the background runner is doing something slow for this caller:
               // a spinner, keys dropped, then what waitFor says (1.1.2)
};

// What a session in SState::Waiting is waiting for, and what it does when
// the answer is there (1.1.2). waitArg says which of that kind's endings.
enum class WaitFor : uint8_t {
    None,
    Space,      // the free-space figures (MEM FORCE, SYS FORCE): arg 0 MEM, 1 SYS
    Screens,    // SCREENS's table, built on the runner: then the list
};

enum class Role : uint8_t {
    Caller,    // nodes 1..BBS_MAX_NODES
    Busy,      // the busy line (all nodes full)
    Sysop,     // hidden sysop node, entered with BYE <password>
};

// PlugRows is a list a plugin builds, row by row, through its rows() hook.
// Everything else here is a list the core builds. Paging, the [More] prompt,
// the abort keys and the backpressure that stops a list outrunning a slow
// terminal all live in the core, and a plugin that wants a list longer than a
// screen has no way to reimplement any of that correctly. Plugins:: is the
// PLUGINS command and is unrelated; the names are close because the two
// things are not.
enum class ListKind : uint8_t { None, Help, Who, Last, Nodes, Bans, Dash, Users, Plugins,
                                Sys, Calls, PlugRows, Screens, Hardware };
enum class MoreFrom : uint8_t { List, Screen };
// ConfigArea is a CONFIG page opened from a button on another CONFIG page:
// one level of nesting, which is what a row standing for several values
// needs and as deep as settings have ever had to go. The page it came back
// to is remembered with the rest of the CONFIG editing state, so a caller
// dropping the line inside one releases the editor exactly as before.
enum class FormKind : uint8_t { None, Signup, Profile, Password, UserAdd, UserEdit, Config,
                                ConfigArea };
enum class ConfirmKind : uint8_t { Logoff, DeleteUser };

// What happens once the caller has pressed a key at a pause.
enum class AfterKey : uint8_t { Prompt, SignupForm, ScreenNext, KnowMore, ConfigClosed };

// Which menu a command appears in. HELP with no argument shows Main, the
// handful people use all the time, and names the other menus.
enum class Menu : uint8_t {
    Main,       // the everyday commands
    Chat,       // the chat room and messaging
    Account,    // your account and other callers
    Staff,      // staff tools
    Sysop,      // sysop only
    Hidden,     // never listed
    None,       // not a menu at all: what menuFromText says for a word it
                // does not know, so HELP can answer instead of guessing
};

struct Session {
    int          fd          = -1;
    uint8_t      id          = 0;      // node number; 0 sysop, BBS_MAX_NODES+1 busy
    SState       st          = SState::Free;
    Role         role        = Role::Caller;
    bool         wantWrite   = false;  // last send would block
    bool         negotiated  = false;  // telnet options sent
    uint8_t      rxBuf[BBS_RX_CHUNK] = {};   // filtered input not yet handled
    uint8_t      rxLen       = 0;            // (held while the output buffer is full)
    uint8_t      rxPos       = 0;
    char         ip[16]      = {};
    uint32_t     ipAddr      = 0;      // raw s_addr
    uint32_t     connectedAt = 0;
    uint32_t     lastInput   = 0;
    uint32_t     lastRx      = 0;
    uint32_t     closeAt     = 0;
    uint32_t     lingerAt    = 0;    // earliest drop once the send-off is out
    bool         noLimits    = false; // TIME n -1: no clock, no idle hangup
    uint8_t      fxStep      = 0;
    uint16_t     savedCps    = 0;
    bool         pendingPrompt = false;  // prompt once the screen finishes
    FormKind     pendingForm = FormKind::None;  // form to open once it finishes
    AfterKey     afterKey    = AfterKey::Prompt; // what a pause leads to
    bool         pendingTail   = false;  // hangup tail after goodbye screen
    bool         pendingKnowMore = false; // rules screen leads to the warning
    bool         newAccount      = false; // first call: show newuser, not motd
    uint8_t      setupStage      = 0;     // first-boot setup: 1 CONFIG staff next, 2 newsysop next
    uint32_t     heapAtOpen  = 0;
    char         user[BBS_USER_MAX + 1] = {};

    // call: which call this is, since boot (1.1.2). A job the background
    // runner does for a caller carries the node AND this, never a Session*:
    // the caller may have hung up by the time the answer comes back, and
    // the slot may be somebody else's. openSession numbers every call.
    uint16_t     call        = 0;
    // SState::Waiting: for what, which ending, since when, and the spinner.
    WaitFor      waitFor     = WaitFor::None;
    uint8_t      waitArg     = 0;
    uint32_t     waitFrom    = 0;
    uint32_t     waitSpinAt  = 0;

    // A message being written, whichever subsystem is taking it. One per
    // session rather than one per subsystem: a caller writes one thing at a
    // time, and forums and mail each keeping their own is what overflowed
    // DRAM by 8,200 bytes.
    char         compose[BBS_COMPOSE_MAX + 1] = {};

    // login, idle, time limits
    bool         loggedIn    = false;
    uint32_t     loginAt     = 0;
    uint32_t     loginEpoch  = 0;
    uint16_t     dayUsedMin  = 0;      // minutes used earlier today
    int16_t      timeAdjMin  = 0;      // sysop TIME adjustments
    uint8_t      timeWarned  = 0;      // 0 none, 1 five-minute, 2 one-minute
    bool         idleWarned  = false;
    uint32_t     busyLoginUntil = 0;   // busy-line login deadline
    bool         guest       = false;  // logged in as GUEST: no account, nothing saved
    uint8_t      rank        = 0;      // staff rank of the account (Access value), for
                                       // the WHO/NODES/DASH marker; raised on elevation
    char         doing[BBS_DOING_MAX + 1] = {};   // last command verb (staff WHO/DASH)
    uint8_t      owner       = 0xFF;   // plugin holding this session, 0xFF none
    // rawInput: the owning plugin wants bytes, not keys. For a file
    // transfer, where the terminal layer's key decoding would eat half the
    // protocol: an 0x1B in a data block is not the start of an escape
    // sequence and an 0x0D is not Enter.
    bool         rawInput    = false;
    uint32_t     ownerData   = 0;      // scratch for that plugin, cleared on own/release

    // paging and generated lists
    ListKind     list        = ListKind::None;
    uint8_t      listPlugin  = 0xFF;   // ListKind::PlugRows: whose rows these are
    uint8_t      listIdx     = 0;
    uint8_t      listSub     = 0;      // wrap offset inside the current row
    uint8_t      pageLines   = 0;
    bool         nonstop     = false;
    // listHold (1.1.2): the row asked for is not ready (a plugin's list
    // built on the background runner). No row was drawn and none taken:
    // the list waits and asks for the same row again next pass.
    bool         listHeld    = false;
    MoreFrom     moreFrom    = MoreFrom::List;

    // WHO n / DASH n refresh
    Menu         helpMenu    = Menu::Main;   // which HELP section is being drawn
    bool         helpAll     = false;        // ? all: walk every section in turn
    ListKind     watch       = ListKind::None;
    uint8_t      watchSecs   = 0;
    // DASH (1.1.0): the page this caller is on, and the node row picked with
    // Up and Down, 0xFF for none. The pick is a row in node order (1 to
    // BBS_MAX_NODES, then S, then B), so it stays on the same line across
    // refreshes however the callers come and go. Bit 7 of dashPage is DASH
    // ALL walking every page as one paged list.
    uint8_t      dashPage    = 0;
    uint8_t      dashSel     = 0xFF;
    // The busy line's countdown, here rather than beside nextTick: in the
    // padding before watchNext, which is where the two DASH bytes went too,
    // so the pair costs no Session anything (every byte there costs twelve).
    uint8_t      countdown   = 0;
    // HARDWARE and SYS (1.1.1): what the board had running when the list
    // started (Bbs::hwSnap), so a card or camera arriving while a caller
    // sits at [More] cannot make the capability line repeat or skip. The
    // last two bytes of that padding: still no cost to a Session.
    uint16_t     hwCaps      = 0;
    uint32_t     watchNext   = 0;      // next redraw; 0 = drawing now

    // busy line countdown: when the next second is due (countdown is above)
    uint32_t     nextTick    = 0;

    // accounts, forms, user manager
    uint8_t      passTries   = 0;
    FormKind     formKind    = FormKind::None;
    ConfirmKind  confirm     = ConfirmKind::Logoff;
    bool         backToUsers = false;  // a staff form returns to the user manager
    uint8_t      ulSel       = 0;      // user manager: selected index
    uint8_t      ulTop       = 0;      //               first index on screen
    uint8_t      ulCount     = 0;
    char         origHandle[BBS_USER_MAX + 1] = {};
    char         pwA[BBS_PASS_MAX + 1] = {};
    char         pwB[BBS_PASS_MAX + 1] = {};
    char         pwC[BBS_PASS_MAX + 1] = {};
    char         yesno[2]    = "N";
    char         levelBuf[8] = "User";   // staff form: the Level field
    char         landBuf[10] = "Default"; // account form: the Start field
    bool         pendingLand = false;    // the login screen ends, then they land
    bool         landing     = false;    // true only while the landing command runs,
                                         // so a plugin can tell "put here at login"
                                         // from "typed the command".
    UserRec      edit;                 // account being logged in or edited
    FormField    fields[Form::kMaxFields];
    Form         form;

    // staff access (BYE <password>)
    Access       level       = Access::None;
    uint16_t     perms       = 0;      // Perm bits from the [access] matrix

    // presence
    bool         dnd         = false;  // refuse pages
    // BELL and the room's /b: no bells caused by other callers (pages,
    // broadcasts, arrivals, the room, @BELL@). One setting for the call,
    // on the session rather than in the chat plugin, because the main
    // prompt rings too and a setting the room reset on every visit was two
    // settings pretending to be one.
    bool         bellOff     = false;
    bool         visible     = true;   // listed in WHO (sysop default: hidden)
    bool         lurk        = false;  // staff: hidden and pages off
    int8_t       histPos     = -1;     // -1 = editing a fresh line
    Session*     snooper     = nullptr;// sysop session mirroring this output

    Term         term;
    Telnet       tn;
    Detector     det;
    LineEditor   ed;
    LineHistory  hist;
    Mailbox      mb;
    ScreenPlayer scr;
    Timeline     tl;
};

class Bbs;

// ---------------------------------------------------------------------------
// Command registry entry. A handler owns its output and ends with a prompt
// (or hands the session to a list, form or screen that prompts later).
// ---------------------------------------------------------------------------
using CmdFn = void (*)(Bbs& bbs, Session& s, const char* arg, uint32_t now);

enum CmdFlag : uint8_t {
    CF_NONE     = 0,
    CF_HIDDEN   = 1,   // works, never listed in HELP (aliases)
    CF_STAFF    = 2,   // needs any staff level (perm 0 staff commands)
    CF_HELPONLY = 4,   // HELP row only, never dispatched (a staff form of a verb)
    CF_ACCOUNT  = 8,   // needs an account: hidden from guests
    CF_READ     = 16,  // plugin command: needs the plugin's read level
    CF_WRITE    = 32,  // plugin command: needs its write level (the default)
    CF_ADMIN    = 64,  // plugin command: needs its admin level
    CF_SYSOP    = 128, // the sysop only, whatever the permission matrix says
};

struct Command {
    const char* verb;    // "WHO"
    const char* keys;    // one-letter shortcuts, "" = none, e.g. "H?"
    uint16_t    perm;    // PERM_* bit needed, 0 = everyone
    uint8_t     flags;   // CmdFlag
    const char* usage;   // HELP column, 12 characters max, e.g. "[W]HO [n]"
    const char* help;    // description, wrapped to the HELP width
    CmdFn       fn;
    Menu        menu = Menu::Main;   // which menu lists it
    uint8_t     rank = 50;           // lower sorts first: 0 is the most used
};

class Bbs {
public:
    static Bbs& instance();

    // begin: open the listener. False if the port cannot be bound.
    bool begin(uint16_t port = BBS_PORT);

    // port: the port the listener is bound to, 0 before begin(). What SYS,
    // announce and anything else that tells somebody where to call should
    // say. Not the configured one: that is used from the next restart, and
    // until then the board is still answering here.
    uint16_t port() const { return port_; }

    // tick: one scheduler pass (select + service every session)
    void tick();

    uint8_t activeNodes() const;

    // publicNodes / publicBusy: capacity and occupancy as the directory and
    // anyone else outside should see them. The sysop's own line counts only
    // while a sysop is on it and has chosen to be visible.
    uint8_t publicNodes() const;

    // preLoginName: what to call a session that has no handle yet.
    //
    // "connecting" while the terminal is still being worked out, "logging
    // in" once it is, because those are genuinely different states and a
    // sysop watching a line stuck on one of them wants to know which.
    static const char* preLoginName(const Session& s);
    uint8_t publicBusy() const;
    static size_t sessionSize() { return sizeof(Session); }

#ifdef BBS_HAS_LCD
    // ----------------------------------------------------------------------
    // What the board's own display reads (BBS_HAS_LCD boards only: the
    // panel plugin). Each is a member the core already keeps, read as it
    // stands: never a file, never a walk of the heap, because the panel asks
    // twice a second. None of them exists on a board without a panel.
    //
    // ringing:    the handle of the caller ringing the sysop right now, or
    //             null. The ring's own limit ends it.
    // backupOpen: the backup window is open (BOOT with the sysop on).
    // slowPasses: passes over BBS_SLOW_PASS_US since boot, SYS's figure.
    // crashBoot:  this boot followed a crash, a watchdog or a brownout.
    // peakNodes:  most lines busy at once since boot.
    // callsToday: the "nth caller today" the last login was told, 0 until
    //             somebody logs in after a boot.
    // ----------------------------------------------------------------------
    const char* ringing() const { return ring_.from != 0xFF ? ring_.handle : nullptr; }
    bool     backupOpen() const { return backup_.isOpen(); }
    uint32_t slowPasses() const { return slowCount_; }
    bool     crashBoot()  const { return bootCrash_; }
    uint8_t  peakNodes()  const { return peakNodes_; }
    uint16_t callsToday() const { return panelToday_; }
#endif

    // key dispatch target (public for the Term callback trampoline)
    void onKey(Session& s, int k, uint32_t now);

    // registerCommands: add a command table (kept by pointer, must be
    // static). plugin is the plugin's index, 0xFF for the core table.
    // False when the registry is full.
    bool registerCommands(const Command* list, uint8_t count, uint8_t plugin = 0xFF);
    // dropPluginCommands: take every plugin's table back out, keeping the
    // core's. Called before the plugins are restarted by a config reload.
    void dropPluginCommands(uint32_t mask = 0xFFFFFFFFu);
    // closeCardScreens: end any screen being read from the SD card and hand
    // those callers back to the prompt. Called before the card is unmounted,
    // and before RESTORE SD SCREENS replaces the card's screens, which says
    // so in why (the default is the unmount's line).
    void closeCardScreens(const char* why = nullptr);
    // endScreens: the same for the screens being read from one place: the
    // card (card) or the board's own flash (!card). A restore that replaces
    // screens calls it before each file goes in (1.1.0). A caller not yet
    // logged in is moved on as their screen ending would have moved them,
    // never left at a prompt they have no account for.
    void endScreens(bool card, const char* why);
    // dropCardJob: a backup being written to the card, or a zip being
    // checked off it, lets go of its files, for the same reason (1.1.0). The
    // nightly one is the case that matters: it runs while the sysop is free
    // to type SD UNMOUNT. A restore already being put live is not stopped.
    void dropCardJob();

    // -- for plugins ---------------------------------------------------------
    // own: the plugin takes this session: keys go to its onKey hook and the
    // idle clock pauses. release() hands it back to the command prompt.
    bool own(Session& s, uint8_t plugin);
    void release(Session& s);

    // showScreen: draw one of the board's screens to a caller now, in one
    // go, leaving the session state alone.
    //
    // This is not playScreen. That one hands the session to the screen
    // player and returns the caller to the prompt afterwards, which is
    // exactly wrong for a plugin that owns the session and wants the caller
    // to end up inside it. Paging is off for the same reason: the caller
    // asked to go somewhere, not to read something.
    //
    // For short transition screens only. It stops early rather than
    // overrun the caller's timeline. False when the board has no such
    // screen, in which case the plugin simply carries on.
    bool showScreen(Session& s, const char* name);
    bool owns(const Session& s, uint8_t plugin) const;

    // sayTo: one line on a caller's screen, redrawing whatever they were at
    void sayTo(Session& s, Color c, const char* text);

    // eachSession: every session, free ones included
    using EachFn = void (*)(void* ctx, Session& s);
    void eachSession(EachFn fn, void* ctx);

    // doing: what a plugin wants shown in the staff WHO/DASH column
    void setDoing(Session& s, const char* what);

    // prompt: draw the command prompt and take a line (plugins end here)
    void prompt(Session& s);

    // ----------------------------------------------------------------------
    // Paged lists for plugins.
    //
    // startPluginList hands the session to the core's list machinery, which
    // then calls this plugin's rows() hook once per line until it returns
    // false. The plugin gets paging, the [More] prompt, the abort keys and
    // the backpressure that stops a list outrunning a slow terminal, none of
    // which it could reimplement correctly on its own.
    //
    // The row index is Session::listIdx, the same counter the core's own
    // lists use, so a plugin's rows() looks like rowWho or rowLast.
    //
    // The row helpers are here rather than private because a plugin drawing
    // its own rows should produce rows that look like everything else: same
    // title bar, same closing rule, same width discipline.
    // ----------------------------------------------------------------------
    // setRawInput: hand this session's bytes to the owning plugin's
    // onBytes instead of decoding them into keys. Only while a plugin owns
    // the session; turning it on without owning it does nothing.
    void setRawInput(Session& s, bool on);
    void startPluginList(Session& s, uint8_t plugin);
    // listHold: inside a plugin's rows(), "not yet": return true after it,
    // having drawn nothing and taken no row (1.1.2, for rows built on the
    // background runner). The list asks for the same row next pass.
    void listHold(Session& s) { s.listHeld = true; }
    void rowText(Session& s, Color c, const char* text, bool newline = true);
    void rowRule(Session& s);
    void rowTitle(Session& s, const char* title, const char* right = nullptr);
    // The same bar in a colour of the caller's choosing. rowTitle is this
    // with Cyan; a subsystem that wants to mark a row differently, or that
    // is drawing a string a caller typed, uses this. The title is truncated
    // to the row rather than allowed to wrap.
    void rowBar(Session& s, Color c, const char* title, const char* right = nullptr);
    uint8_t rowWidth(const Session& s) const;

    // minutesLeft: how long this call has, for a plugin that wants to say.
    //
    // -1 means no limit, which is a guest with none, a caller the sysop has
    // taken off the clock, or staff. A plugin asking this is usually
    // answering somebody who wants to know whether to start something, so
    // rounding up is right: 30 seconds left is "1 minute", never "0".
    //
    // Public because secondsLeft and unlimited are not, and a plugin has no
    // business reaching for either: this is the question they actually have.
    int32_t minutesLeft(const Session& s, uint32_t now) const;

    // Shell handlers a plugin may reuse, so the room and the main prompt
    // cannot drift into two answers for one question.
    //
    // cmdInfo is WHOIS: it already hides UF_PRIVATE fields from everybody
    // but the owner and PERM_USERS staff, and the room's /whois must not
    // reimplement that rule. cmdPage is PAGE. cmdTimeAdjust is
    // TIME n +/-m and does its own permission check.
    //
    // None of them draws a prompt: the command table does that separately,
    // which is what makes them safe to call from a plugin that owns the
    // session and wants to re-arm its own input afterwards.
    void cmdInfo(Session& s, const char* arg);
    void cmdPage(Session& s, const char* arg);

    // The sysop page (bbs_ring.cpp, 1.1.0). cmdOperator is OPERATOR, and the
    // room's /o for anybody but the sysop; for the sysop a bare O asks again
    // about a ring still waiting. ringAnswer and ringDecline are the room's
    // /o and /o- for the sysop, who in the room gets no one-key question.
    // Like cmdPage, none of them draws a prompt or re-arms a plugin's input,
    // and every line they print is ended.
    void cmdOperator(Session& s, const char* arg);
    bool ringAnswer(Session& s);
    bool ringDecline(Session& s);

    // notify: tell a caller something at their next prompt, the way PAGE
    // does, rather than writing into whatever they are looking at. The core
    // lifts the prompt, says it, and puts the prompt back.
    void notify(Session& to, const char* text);
    void cmdTimeAdjust(Session& s, const char* arg);

    // findCommand: the command a verb or shortcut runs for this caller, or
    // null. Public so the room's /? <command> can answer for main prompt
    // words by the same rule: a command the caller may not use is unknown.
    const Command* findCommand(const char* verb, const Session& s) const;

    // codesSummary: the inline @-codes in a dozen lines, for CODES on a
    // board with no screens/codes and for the room's /codes, where a paged
    // screen cannot be played. Prints and draws no prompt.
    void codesSummary(Session& s);

    // logoff: end the call exactly as BYE does, send-off screen, linger and
    // all, for a plugin that offers its own way out (the room's /q+). The
    // session leaves SState::Plugin, so a release() after it draws no prompt.
    void logoff(Session& s, uint32_t now) { goodbye(s, now); }

    // ----------------------------------------------------------------------
    // The lines, as something outside the shell would show them: the lights
    // plugin's Hayes panel and node strip. Reads only, and cheap ones.
    //
    // listening: the dial-in port is open. answering: it is also taking
    // calls, which a SHUTDOWN, counting down or done, stops.
    // takeTraffic: which sessions moved bytes since the last ask, one bit
    // per Session::id (0 the sysop, 1 to BBS_MAX_NODES the lines, then the
    // busy line), and clears them. One reader: the lights.
    // bytesIn / bytesOut: every byte read from and written to a caller's
    // socket since boot, wrapping. Only a difference means anything.
    // ----------------------------------------------------------------------
    bool listening() const { return lfd_ >= 0; }
    bool answering() const { return lfd_ >= 0 && !shutEnds_ && !shutDone_; }
    void takeTraffic(uint16_t& rx, uint16_t& tx) {
        rx = rxSeen_;
        tx = txSeen_;
        rxSeen_ = txSeen_ = 0;
    }
    uint32_t bytesIn()  const { return rxBytes_; }
    uint32_t bytesOut() const { return txBytes_; }

    // sysopMail: the sysop has mail not yet read (1.1.0), missed rings
    // included, which go to MAIL now. A flag the chat plugin keeps in RAM:
    // cheap enough for a display to ask on every frame, for its letter icon.
    // False when chat, and so mail, is not running.
    bool sysopMail() const;

    // The sysop's own account (1.1.0, Rob): the one a missed ring is mailed
    // to, whose unread mail sysopMail() means, and who is asked for the
    // sysop password at login. CONFIG board's "Sysop" (sysop_id) while that
    // account exists and is not retired; otherwise the last account to
    // elevate to sysop (sysopLast_, kept in userdata so it outlives a
    // restart). Matched by id, never by handle.
    //
    // sysopAccount: one pass over users.txt, filling out. False when there is
    // no such account. At a ring's end or a plugin start, never in a frame.
    bool sysopAccount(UserRec& out);
    // isSysopAccount: is this id the sysop's account? For a caller who
    // already has the account in hand (a login, a message being stored),
    // so the common answers need no read: the configured id, or no id at
    // all, answer at once, and only "the fallback, while an id is
    // configured" reads users.txt to see whether the configured one is live.
    bool isSysopAccount(uint32_t id);

private:
    Bbs() = default;

    static constexpr uint8_t kSessions      = BBS_MAX_NODES + 2;   // + busy + sysop
    static constexpr uint8_t kCommandTables = 12;                  // core + plugins

    // -- connections (bbs.cpp) ---------------------------------------------
    void acceptAll(uint32_t now);
    void openSession(Session& s, int fd, const char* ip, uint32_t ipAddr, Role role, uint32_t now);
    void closeSession(Session& s, const char* why, uint32_t now);
    void readSession(Session& s, uint32_t now);
    void processInput(Session& s, uint32_t now);
    void serviceSession(Session& s, uint32_t now);
    // screenEnded: a screen played in the shell has finished, or been cut
    // short: go wherever it was leading (a form, the warning, setup, the
    // landing, the prompt). False when it was leading nowhere (1.1.0).
    bool screenEnded(Session& s, uint32_t now);
    void flush(Session& s, uint32_t now);
    void moveSession(Session& from, Session& to, uint8_t newId, Role role);

    // -- flow (bbs.cpp) -------------------------------------------------------
    void onDetected(Session& s, uint32_t now);
    void startIntro(Session& s);
    void startBusy(Session& s, uint32_t now);
    // linkLine: how this caller is connected and whether it is encrypted,
    // said before any screen (1.1.1, Rob): "--> Connection via Telnet is
    // not secure". Every caller who is detected sees it, on the welcome,
    // the busy line and the closed sign alike.
    void linkLine(Session& s);
    // Closed to callers (1.1.0, CONFIG board "Stop taking calls"). A caller
    // gets the busy line's sign and countdown in the closed wording, and a
    // key opens a login that only the sysop's own account gets past
    // (closedAdmits). A board with no accounts at all skips the sign: its
    // first caller registers, and that account is the one let in after.
    bool closedTo(const Session& s) const;            // this caller meets the closed board
    bool closedAdmits(uint32_t id);                   // the one account a closed board takes
    void closedRefuse(Session& s, uint32_t now);      // said the same for every handle
    void staffLanding(Session& s, uint32_t now, bool atLogin);   // after an elevation
    void askName(Session& s);
    void armName(Session& s);
    void loginHint(Session& s);
    void drawNamePrompt(Session& s);
    void onHandle(Session& s, uint32_t now);
    void onNewHandle(Session& s, int k, uint32_t now);
    void askKnowMore(Session& s);
    void pauseFor(Session& s, AfterKey then);
    void showPrivacy(Session& s, AfterKey then);
    void showRules(Session& s);
    void onAnyKey(Session& s, uint32_t now);
    void onKnowMore(Session& s, int k, uint32_t now);
    void cmdPrivacy(Session& s);
    bool handleOnline(const Session& s, const char* handle) const;
    void loginGuest(Session& s, uint32_t now);
    void inputError(Session& s, uint8_t used, const char* longMsg, const char* shortMsg);
    void askPassword(Session& s);
    void onPassword(Session& s, uint32_t now);
    void completeLogin(Session& s, uint32_t now);
    void arrive(Session& s);                          // motd or newuser, then landing
    bool offerSetup(Session& s);                      // unconfigured board, local caller
    void askSetup(Session& s);
    void onSetupPassword(Session& s, uint32_t now);
    void skipSetup(Session& s);                       // ESC at the setup question
    void beginSetup(Session& s, uint32_t now);        // setup screen, then setupConfig
    void setupConfig(Session& s, uint32_t now);       // CONFIG staff, as part of setup
    // The sysop's account at login (1.1.0): "Sysop password:" there and
    // then, Enter skips. Through staffPassword, so every rule BYE applies
    // applies here. Never granted by the account password alone.
    bool offerSysop(Session& s);
    void askSysop(Session& s);
    void onSysopPassword(Session& s, uint32_t now);
    void saveCallStats(Session& s, uint32_t now);
    uint16_t dayMinutesUsed(const char* handle, uint32_t now);

    // -- accounts and forms (bbs_users.cpp) -----------------------------------
    void addField(Session& s, uint8_t& n, const char* label, char* buf, uint8_t cap, uint8_t flags,
                  const char* choices = nullptr);
    uint8_t addUserFields(Session& s, uint8_t n);
    void startForm(Session& s, FormKind kind, uint32_t now);
    void formSave(Session& s, uint32_t now);
    void formCancel(Session& s, uint32_t now);
    // formOpen: an FF_ACTION button was pressed on the form this session is
    // holding. Only CONFIG has buttons today; anything else ignores it.
    void formOpen(Session& s, uint8_t field, uint32_t now);
    bool checkUserFields(Session& s, uint8_t firstField);
    void formDone(Session& s, Color c, const char* msg);
    void cmdProfile(Session& s, uint32_t now);
    void cmdPassword(Session& s, uint32_t now);
    void cmdUsers(Session& s, uint32_t now);
    void cmdUser(Session& s, const char* arg, uint32_t now);
    bool rowUsers(Session& s);
    void ulOpen(Session& s);
    void ulStatus(Session& s, Color c, const char* msg);
    void ulDrawRow(Session& s, uint8_t index);
    void ulKey(Session& s, int k, uint32_t now);
    uint8_t ulRows(const Session& s) const;
    void armPrompt(Session& s);
    void drawPrompt(Session& s);
    void hangup(Session& s, const char* msg, uint32_t now);
    void goodbye(Session& s, uint32_t now);
    void exitScreen(Session& s, uint32_t now);

    // noteBoot: write why the board started into the reboot log, and keep it
    // so the next staff member to log in is told. A crash that reboots
    // cleanly is otherwise invisible: the only symptom is an uptime that
    // keeps starting over, which is easy to miss and easier to misattribute.
    void noteBoot();
    bool bootWasCrash() const { return bootCrash_; }
    const char* bootReason() const { return bootReason_; }
    uint16_t bootCrashCount() const { return bootCrashes_; }
    // bootNotice: why the board last started, to a staff member arriving,
    // when it is worth saying: a crash, or a reset somebody did with the BOOT
    // button (1.1.0), which the chip alone reports as a software restart.
    void bootNotice(Session& s);
    // staffArrival: what staff are told as they arrive, on the sysop node or
    // on their own line: why the board last restarted, the rings nobody
    // answered (the sysop only), and last night's backup if it failed. On
    // the setup path the setup screen's @CLS@ would wipe all three, and the
    // ring notes are deleted once shown, so there they are owed instead and
    // said at the first prompt after the setup (noticeOwed_).
    void staffArrival(Session& s);
    bool playScreen(Session& s, const char* name);

    // -- timers, notices, paging, refresh (bbs.cpp) --------------------------
    void checkTimers(Session& s, uint32_t now);
    int32_t secondsLeft(const Session& s, uint32_t now) const;
    int32_t idleSecondsLeft(const Session& s, uint32_t now) const;
    bool canNotify(const Session& s) const;
    void notify(Session& s, Color c, const char* msg);
    void warnNow(Session& s, const char* msg);
    // warnElsewhere (1.1.2): a timer warning for a caller the prompt's path
    // cannot reach: inside a plugin, through its liftInput/restoreInput, or
    // at a screen's page break. In the board's own voice, "--> 5 minutes left
    // on this call", with the bell. False when not now: the caller is asked
    // again next pass.
    bool warnElsewhere(Session& s, const char* msg);
    // markedLine: "--> text" from column 0, wrapped at the row width with the
    // text's own column kept; in the room, the room's voice (chat::roomSay).
    void markedLine(Session& s, Color c, const char* text);
    void redrawInput(Session& s);
    void deliverMail(Session& s);
    // deliverMail's three shapes (1.1.0): at the prompt, inside a plugin
    // through its liftInput/restoreInput, and the urgent kinds only on a
    // form's status line or above an input that has no room for more.
    void deliverShell(Session& s);
    void deliverPlugin(Session& s);
    void deliverUrgent(Session& s);
    void busLine(Session& s, const BusMsg& m);
    void post(Session& to, BusKind kind, const Session* from, const char* text);
    void noticeAll(const Session& about, const char* text,
                   BusKind kind = BusKind::Notice);
    void startList(Session& s, ListKind kind);
    void serviceList(Session& s);
    // listEnded: the shell prompt, or back to the plugin that owns the
    // session if a subsystem started this list
    void listEnded(Session& s, bool aborted = false);
    void showMore(Session& s, MoreFrom from);
    void abortOutput(Session& s);
    uint8_t pageRows(const Session& s) const;
    void startWatch(Session& s, ListKind kind, uint8_t secs);
    void serviceWatch(Session& s, uint32_t now);
    void stopWatch(Session& s);

    // -- output helpers (bbs_shell.cpp) --------------------------------------
    void rowSeg(Session& s, Color c, const char* text, uint8_t& col);
    void rowEnd(Session& s, uint8_t col);
    void statRow(Session& s, const char* label, const char* value, Color c = Color::LightGreen,
                 const char* note = nullptr);
    void statNum(Session& s, const char* label, uint32_t value, const char* note = nullptr);
    // statKept: a figure that is kept rather than read now (core/space.h,
    // 1.1.2), with the trailing "." that says so. keptNote: the one line
    // under them saying when they were taken and how to take them again.
    void statKept(Session& s, const char* label, bool known, uint32_t value, const char* note);
    void keptNote(Session& s, const char* force);
    // startWait: SState::Waiting with a spinner after text, until what the
    // runner is doing for this caller is done; serviceWait is its pass.
    void startWait(Session& s, WaitFor what, uint8_t arg, const char* text);
    void serviceWait(Session& s, uint32_t now);
    void rowSection(Session& s, const char* name);
    bool rowSys(Session& s);
    // HARDWARE (bbs_hardware.cpp, 1.1.1): the board's spec sheet, for every
    // caller, with the live figures for staff. hwRow is line k of the
    // section, shared with SYS so the two cannot drift; false past the end.
    // inSys leaves out the heap rows SYS already has under "memory".
    bool rowHardware(Session& s);
    bool hwRow(Session& s, uint8_t k, bool inSys);
    // hwSnap: what the board has running, kept in s.hwCaps as the list
    // starts, so every line of one listing reads the same snapshot (1.1.1).
    static void hwSnap(Session& s);
    bool rowCalls(Session& s);
    void cmdCalls(Session& s);   // padded when refreshing
    // rowText, rowRule, rowTitle and rowWidth are public: a plugin drawing
    // its own paged list has to produce rows that look like everything else.
    static const char* doingText(const Session& s);

    // -- shell (bbs_shell.cpp) -----------------------------------------------
    void runCommand(Session& s, const char* line, uint32_t now);
    void landAfterLogin(Session& s);
    const Command* commandAt(uint8_t index) const;
    static const Command* coreCommands(uint8_t& count);
    // list rows: each call emits exactly one line, false when finished
    bool listRow(Session& s);
    bool rowHelp(Session& s);
    bool rowWho(Session& s);
    bool rowLast(Session& s);
    bool rowDash(Session& s);
    bool rowPlugins(Session& s);

    // -- one node row, for DASH, NODES and WHO (bbs_shell.cpp, 1.1.0) --------
    // Each screen used to format its own node rows, and each got something
    // different wrong: NODES cut PETSCII-40 to PETSCII-4, WHO's rows sat
    // under the wrong headings at 80 columns, DASH showed six lines of ten.
    // One builder per plan, header and rows from the same widths, so a
    // column cannot drift from its heading again.
    //   Wide     80 columns: handle, doing, idle, left, address, terminal (72)
    //   Narrow   40 columns: handle, doing, idle, left (36)
    //   From     40 columns: handle, address, a five letter terminal (37)
    //   Wide132  the Wide row with minutes on before the address (77)
    //   Who      WHO at 60 columns and up; WhoNarrow below that
    enum class NodePlan : uint8_t { Wide, Narrow, From, Wide132, Who, WhoNarrow };
    // nodeAt: node order, the same on every screen: 1 to BBS_MAX_NODES, then
    // the sysop line, then the busy line. nullptr past the end.
    const Session* nodeAt(uint8_t k) const;
    static constexpr uint8_t kNodeRows = BBS_MAX_NODES + 2;
    uint8_t nodeHead(Session& v, NodePlan plan);
    uint8_t nodeCells(Session& v, const Session& o, NodePlan plan);
    // rowClose: the end of a row, padding a highlighted one out in reverse
    // video first so the bar runs the width of the row
    void rowClose(Session& s, uint8_t col, bool highlighted = false);

    // -- the dashboard (bbs_shell.cpp, 1.1.0) ---------------------------------
    // DashSnap: every figure a dashboard frame shows that is not on a
    // Session, taken once at the top of the frame and read by every row
    // after it. The rule it enforces, and the reason it exists: a frame
    // opens no file, walks no heap or filesystem, and makes at most one call
    // into the Wi-Fi task. The old DASH opened the caller log six times a
    // frame and walked the heap under a critical section, once a second for
    // a sysop with DASH 1 up. SYS reads it too, filled with the heap walk it
    // needs for the biggest block, once per listing instead of once per row.
    // One for the board: two sysops watching share it and read figures at
    // most a frame old.
    struct DashSnap {
        plat::NetInfo net;                  // the one Wi-Fi task call
        const char* power      = "";         // plat::powerSave(), "" on the host
        uint32_t heapFree      = 0;          // plat::heapFree(), 0 on the host
        uint32_t heapLow       = 0;          // heapLow_ as the watch keeps it
        uint32_t heapBig       = 0;          // SYS only: largest block
        uint32_t heapTotal     = 0;          // SYS only
        bool     heapFull      = false;      // SYS filled the two above
        uint32_t stackLeast    = 0;          // stackLow_, 0 not measured
        uint32_t dataFree      = 0;          // the user partition's kept free space (core/space.h)
        bool     dataKnown     = false;      // measured yet
        bool     card          = false;
        bool     cardKnown     = false;      // the card's free space measured yet
        uint32_t cardFreeKB    = 0;
        uint32_t cardTotalKB   = 0;
        uint8_t  bans          = 0;
        uint16_t today         = 0;          // calls today, from calllog's kept count
        char     dir[12]       = "";         // announce's state word, "" when it is off
    };
    DashSnap snap_;
    void snapFill(bool full);
    uint8_t dashPages(const Session& s) const;
    bool dashWide132(const Session& s) const;
    bool dashCells(const Session& s) const;   // cursor keys and reverse: ANSI and PETSCII
    void dashKey(Session& s, int k, uint32_t now);
    void dashTitle(Session& s);
    void dashWaiting(Session& s);
    void dashSeg(Session& s, uint8_t& col, uint8_t gap, const char* label, const char* value,
                 bool alarm);
    void dashVitals(Session& s, uint8_t which);
    void dashCallsHead(Session& s);
    void dashNode(Session& s, uint8_t k, NodePlan plan);
    void dashCall(Session& s, uint8_t back, NodePlan plan);
    bool dashPluginRow(Session& s, uint8_t which);
    bool dashBanRow(Session& s, uint8_t which);
    void dashRight(Session& s, uint8_t& col, uint8_t which);
    void dashFooter(Session& s, char* out, size_t n);
    // One row of a page: 0 the page is done, 1 drew a row, 2 nothing to draw
    // (a blank kept only for a refreshing frame's height)
    uint8_t dashPage80(Session& s, uint8_t page, uint8_t i);
    uint8_t dashPage40(Session& s, uint8_t page, uint8_t i);
    uint8_t dashPage132(Session& s, uint8_t i);
    bool rowWatchFooter(Session& s);
    void cmdHelp(Session& s, const char* arg);
    // longHelp: HELP <command>, one command in full, from helptext.
    void longHelp(Session& s, const Command& c);
    bool helpRow(Session& s, uint8_t index, uint8_t plugin);
    void helpUsage(Session& s, const char* usage, bool dim);
    bool helpWanted(const Session& s, const Command& c) const;
    bool helpEmpty(const Session& s) const;
    void cmdWho(Session& s, const char* arg);
    void cmdNodes(Session& s, const char* arg);
    void cmdShutdown(Session& s, const char* arg, uint32_t now);
    void serviceShutdown(uint32_t now);
    bool shuttingDown() const { return shutEnds_ != 0; }
    void cmdDash(Session& s, const char* arg);
    void cmdMem(Session& s);
    // MEM and SYS with FORCE (1.1.2): measure the kept free-space figures
    // again on the runner, the spinner meanwhile, then draw. Staff only.
    void cmdMemArg(Session& s, const char* arg);
    void cmdSysArg(Session& s, const char* arg);
    void cmdAbout(Session& s);
    void cmdTerm(Session& s);
    void cmdTime(Session& s, const char* arg, uint32_t now);
    void cmdBaud(Session& s, const char* arg);
    void cmdDnd(Session& s);
    void cmdBell(Session& s);
    void cmdCodes(Session& s);
    void cmdBye(Session& s, const char* arg, uint32_t now);
    // staffPassword: the one check a typed staff password meets, at BYE and
    // at the sysop account's login question (1.1.0). The level it buys, with
    // the published default honoured from the board's own network only; a
    // right one clears the address's ban count. Access::None for a wrong
    // one, which is logged and counted toward the address's ban, and
    // *banned says whether that count just banned it.
    Access staffPassword(Session& s, const char* pw, uint32_t now, bool* banned);
    void fxNext(Session& s);

    // -- sysop (bbs_sysop.cpp) -----------------------------------------------
    void markAccount(Session& s, Access level);
    // atLogin: through the sysop account's login question (offerSysop),
    // which on a closed board goes on to CONFIG board (staffLanding).
    void elevate(Session& s, uint32_t now, bool setup = false, bool atLogin = false);
    static bool localAddr(const char* ip);   // RFC1918, loopback, link-local
    void rememberStaff(const Session& s, Access level);
    void restoreStaff(Session& s);
    void coElevate(Session& s, Access level, uint32_t now, bool setup = false, bool atLogin = false);
    bool rowNodes(Session& s);
    bool rowBans(Session& s);
    void cmdKick(Session& s, const char* arg, uint32_t now);
    void cmdBroadcast(Session& s, const char* arg);
    void cmdSnoop(Session& s, const char* arg);
    void stopSnoop(Session& s, const char* why);

    // unlimited: this call is not on the clock, either because of the
    // caller's rank or because a sysop said so for tonight. Defined in
    // bbs.cpp: can() lives in bbs_util.h, which includes this header.
    bool unlimited(const Session& s) const;
    void cmdUnban(Session& s, const char* arg);
    void cmdDrop(Session& s, uint32_t now);
    void cmdConfig(Session& s, const char* arg, uint32_t now, uint8_t focus = 0);
    void configClosedRow(Session& s, uint32_t now);  // CONFIG board on "Stop taking calls"
    void configPages(Session& s);
    bool configSave(Session& s, char* err, size_t errLen);
    void configRelease(const Session& s);
    // The page stack. A row whose value is several values packed with bars
    // is drawn as a button; pressing it opens that row as a page of its
    // own, and saving or cancelling comes back to the row it came from.
    void configOpenPage(Session& s, uint8_t focus, uint32_t now);
    void configSubOpen(Session& s, uint8_t field, uint32_t now);
    bool configSubSave(Session& s, char* err, size_t errLen);
    void configSubBack(Session& s, Color c, const char* msg, uint32_t now);
    // A plugin's PS_PAGE button: its rows as a page of their own, whose
    // buttons open sub-pages as above. configInList says one is open, and
    // configListBack returns to the plugin's page, on the button.
    void configListOpen(Session& s, uint8_t field, uint32_t now);
    bool configInList() const;
    void configListBack(Session& s, Color c, const char* msg, uint32_t now);
    // configReloadAll: reread system.cfg and restart the plugins, having
    // first handed home any caller sitting inside one. Shared by the page
    // and the sub-page so a save means the same thing from either.
    bool configReloadAll(char* err, size_t errLen);
    void cmdShow(Session& s, bool show);
    // presenceChanged: tell the plugins the public picture moved
    void presenceChanged(Session& s);
    void cmdLurk(Session& s);
    Session* nodeByArg(const char* arg, const char** rest);

    // -- backup window (bbs.cpp) ---------------------------------------------
    void serviceBackup(uint32_t now);
    void showApproval(Session& s);

    // -- the sysop page (bbs_ring.cpp) --------------------------------------
    // How a ring ended. The order is the log's words in ringEnd.
    enum class RingEnd : uint8_t { Answered, Declined, Away, NoAnswer, Stopped, HungUp, Gone };
    Session* sessionById(uint8_t id);
    Session* ringTarget(bool& away);
    bool ringLive(const Session& s, const BusMsg& m) const;
    void ringRefused(Session& s, uint8_t verdict, uint8_t minutes);
    void ringWhyKey(Session& s, int k, uint32_t now);
    bool ringStart(Session& s, const char* reason, uint32_t now);
    void ringStopSpin(Session& c);
    void ringBack(Session& s);
    void ringNotice(Session& s, bool fanfare);
    void ringAsk(Session& s, bool fanfare);
    bool ringShow(Session& s, bool room);
    void ringFormHint(Session& s);
    void ringAskKey(Session& s, int k, uint32_t now);
    void ringingKey(Session& s, uint32_t now);
    void ringYank(Session& s);
    void ringClear();
    void ringEnd(RingEnd how);
    void ringClosed(Session& s);
    void serviceRing(uint32_t now);
    // ringLeave: where a ring nobody answered goes (1.1.0, Rob). MAIL, from
    // the caller, to the sysop's account (sysopAccount); the note file when
    // there is no such account, mail is off, or the box is full, so a ring
    // is never lost. True when it went to mail.
    bool ringLeave(const ring::Note& n);
    void ringSaveNote(const ring::Note& n);
    // ringNotes: at the sysop's login or elevation, and at a bare O with no
    // ring live (1.1.0). False when there was nothing to show, or no room to
    // show it in, in which case the notes are kept.
    bool ringNotes(Session& s);
    // ringNoteCount: the notes on file, read once at boot so the dashboard
    // knows without opening the file again
    uint16_t ringNoteCount();

    // One ring at a time on the whole board, so this is all the state there
    // is: about a hundred bytes, and nothing on a Session. The caller and
    // the sysop are Session::ids, looked up each time: sessions come from a
    // static pool and moveSession changes an id, both of which make a held
    // pointer the stale kind.
    struct RingState {
        uint32_t seq     = 0;          // numbers the rings, so a stale notice is dropped
        uint8_t  from    = 0xFF;       // the caller ringing, 0xFF when nobody is
        uint8_t  to      = 0xFF;       // the sysop session rung
        bool     guest   = false;
        bool     shown   = false;      // the sysop has seen it, one way or another
        uint8_t  spin    = 0;          // spinner frame
        uint32_t started = 0;          // millis it began
        uint32_t ends    = 0;          // millis it runs out
        uint32_t spinAt  = 0;          // millis of the next spinner frame
        char     handle[BBS_USER_MAX + 1]     = {};
        char     reason[ring::kReasonMax + 1] = {};
    };
    RingState ring_;
    // Ring notes waiting for the sysop (1.1.0): counted at boot, raised by
    // ringSaveNote, cleared when ringNotes shows them. What the dashboard's
    // "Waiting on you" row says, without opening rings.txt per frame.
    uint16_t  ringNotesWaiting_ = 0;
    // The last account to elevate to sysop (1.1.0): the fallback for where
    // missed rings go when CONFIG names no sysop account, or names one that
    // is gone. In <userdata>/sysop.last, read at boot, written only when it
    // changes. 0 is nobody yet.
    uint32_t  sysopLast_ = 0;
    void      sysopLastLoad();
    void      sysopLastSave(uint32_t id);
    // linkSysop: CONFIG board's Sysop set to this caller's account, by the
    // setup flow, for the account that set the board up.
    void      linkSysop(const Session& s);

    // -- backups on the SD card (bbs_backup.cpp, 1.1.0) ------------------------
    void cmdBackup(Session& s, const char* arg, uint32_t now);
    void cmdRestore(Session& s, const char* arg, uint32_t now);
    void serviceCard(uint32_t now);
    void cardKey(Session& s, int k);
    void cardAsk(Session& s, uint32_t now);
    void cardRow(Session& s, const char* label, const char* value, Color c);
    void cardDotsOut(Session& s, uint8_t n, uint8_t cap);
    void cardDone(Session& s, BackupService::Job was, bool watching, uint32_t now);
    void nightlyTick(uint32_t now);
    void nightlyDone();
    // nightlyNotice: last night's backup did not happen, to staff arriving
    void nightlyNotice(Session& s);
    // A restore the sysop said Y to waits for the board to go quiet (1.1.0,
    // BackupService::holding). windowAccepted: the Y to the window's
    // question. holdBegin: after either Y, wait or go. holdGo: put it live,
    // warning anybody still on when forced (F). serviceHold: a pass of the
    // wait. othersOn: callers on the board other than the sysop.
    void windowAccepted(Session& s, uint32_t now);
    void holdBegin(Session& s, uint32_t now);
    void holdGo(Session& s, bool forced);
    void serviceHold(uint32_t now);
    uint8_t othersOn() const;

    // -- SCREENS (bbs_screens.cpp, 1.1.0) --------------------------------------
    // Every screen by name, what callers get of each and from where, and
    // SCREENS VIEW to play one. Staff. Read from the folders when asked.
    void cmdScreens(Session& s, const char* arg);
    bool rowScreens(Session& s);
    // SCREENS INSTALL [STOCK] (1.1.1): the card's own screens copied into
    // flash so they survive the card being pulled, and the stock set put
    // back. A job of one step a pass, never a burst (Rule no. 1), from
    // tick. screensBusy: one is under way (a backup or restore waits for
    // it, and it for them). screensDrop: the card is going (dropCardJob).
    void serviceScreens(uint32_t now);
    bool screensBusy() const;
    void screensDrop();
    void screensInstall(Session& s, bool stock);
    // The table SCREENS draws from, built on the background runner (1.1.2):
    // who is reading it, whether this caller's is ready, the one line when
    // it could not be made, and giving it back to the heap when nobody is.
    uint8_t screensReaders() const;
    bool screensTableReady(const Session& s) const;
    void screensTableGone(Session& s);
    void screensTableRelease();
    // restartPlugins: hand anybody inside a plugin home, stop them all and
    // start them again on the file as it is now. A CONFIG save and a
    // restore both end here (bbs_sysop.cpp).
    // mask (1.1.2): which plugins by index; all of them unless said.
    void restartPlugins(uint32_t mask = 0xFFFFFFFFu);

    int       lfd_          = -1;
    uint16_t  port_         = 0;             // see port()
    BackupService backup_;
    bool      approvalShown_ = false;
    uint32_t  lastBtnLog_    = 0;

    // The card job as the sysop sees it (bbs_backup.cpp). One of these at a
    // time, board-wide, and only ever the sysop's line: CF_SYSOP commands.
    char      cardName_[cardbak::kNameMax + 1] = {};
    uint32_t  cardZipBytes_  = 0;       // RESTORE: the zip's size, for "In zip"
    uint32_t  cardAskBy_     = 0;       // RESTORE: the question times out here
    uint8_t   cardDots_      = 0;       // dots on the current line
    bool      cardScreens_   = false;   // SCREENS
    bool      cardNightly_   = false;   // the nightly backup: nobody watching
    uint32_t  holdUntil_     = 0;       // a held restore gives up here (1.1.0)
    uint8_t   holdSaid_      = 0;       // callers the sysop was last told of
    // The nightly backup's clock. nightlyDay_ is the clk::dayKey it last
    // tried, nightlyFail_ why that did not happen (0 when it did).
    uint32_t  nightlyDay_     = 0;
    uint32_t  nightlyLookAt_  = 0;
    uint32_t  nightlyNoClock_ = 0;
    uint8_t   nightlyFail_    = 0;
    uint32_t  heapBaseline_ = 0;

    // Running figures for the sysop's system screen. All of them are a few
    // bytes and cost nothing to keep; SYS is where they surface.
    uint32_t  loopAvgUs_   = 0;      // smoothed work per pass, microseconds
    uint32_t  loopMaxUs_   = 0;      // worst pass since boot
    uint32_t  loopPasses_  = 0;      // passes of the scheduler since boot

    // What the worst pass was doing. loopMaxUs_ on its own says a stall
    // happened and nothing about where, which is how the last one got
    // diagnosed by reasoning instead of by measuring, and diagnosed wrong.
    // These name the phase and survive to the SYS screen, so the evidence is
    // on the board rather than only on a console somebody had to be watching.
    uint32_t  slowLogAt_   = 0;      // rate limit: one console line a second
    uint32_t  slowCount_   = 0;      // passes over BBS_SLOW_PASS_US since boot
    uint32_t  slowAt_      = 0;      // millis of the last one, 0 none: DASH shows it red
    const char* worstPhase_ = nullptr;   // which phase owned the worst pass
    uint8_t   worstNode_   = 0;      // and which node, when it was a session
    // And what that caller was doing. "in session" narrows a stall to the
    // caller's own path and no further, which still leaves a screen coming
    // off the card, a listing walking a directory and a users.txt rewrite
    // all equally likely. Session::doing already names the verb for humans.
    char      worstDoing_[BBS_DOING_MAX + 1] = {};

    // Heap watch. A board that ran out of heap rebooted with nothing said,
    // and the only evidence afterwards was MEM's "heap low since boot"
    // counter having gone UP, which can only happen across a restart. That
    // is a terrible way to find out. These record the floor and say so on
    // the way down, so the next one leaves a trail.
    uint32_t  heapLow_     = 0xFFFFFFFFu;  // lowest free seen, this boot
    uint32_t  heapStep_    = 0;            // lowest threshold already logged
    uint32_t  heapCheckAt_ = 0;            // millis of the last sample
    void      heapWatch(uint32_t now);

    // Stack watch. SYS says how little stack has ever been left and never
    // which call left it, so the 1,440 bytes Rob read off 0.22.1 could not be
    // traced to anything. Every new low is logged with the phase and, in a
    // session, the node and its Session::doing, the same way a slow pass is.
    // stackWatch is a cheap look after each phase (plat::stackDeeper); the
    // once-a-second full read in the tail catches whatever that missed.
    uint32_t  stackLow_     = 0;           // least free seen by the watch, bytes
    uint32_t  stackCheckAt_ = 0;           // millis of the last full read
    void      stackWatch(const char* phase, const Session* s);

    // SHUTDOWN. The board going down on purpose, with everybody told first.
    // shutEnds_ is the millis the countdown reaches zero, and zero means no
    // shutdown is running, which is why it is never legitimately zero while
    // one is: the constructor of that value adds 1 if it lands on zero.
    uint32_t  shutEnds_  = 0;
    uint32_t  shutSaid_  = 0xFFFFFFFFu;    // smallest threshold already announced
    bool      shutDone_  = false;          // the lines are closed, stay closed
    uint16_t  callsBoot_   = 0;      // calls answered since boot
    uint16_t  callSerial_  = 0;      // Session::call, every session opened (1.1.2)
    bool      bootCrash_   = false;  // this boot followed a crash or watchdog
    bool      bootNoted_   = false;  // this boot followed a BOOT-hold reset (recovery::Note)
    char      bootReason_[32] = "";  // in words, for the sysop
    uint8_t   bootNote_    = 0;      // the recovery::Note this boot came with, 0 none
    // The Session::id owed the staff arrival notices, 0xFF nobody: the one
    // elevated on the setup path, told at its first prompt after the setup
    // (see staffArrival). Moved with moveSession, forgotten with the call.
    uint8_t   noticeOwed_  = 0xFF;
    uint16_t  bootCrashes_ = 0;      // how many are in the reboot log
    uint8_t   peakNodes_   = 0;      // most nodes busy at once since boot
#ifdef BBS_HAS_LCD
    uint16_t  panelToday_  = 0;      // callsToday(), for the board's display
#endif
    uint16_t  callHours_[24] = {};   // CALLS: calls per hour of the day
    uint16_t  callsCounted_ = 0;     // records that went into callHours_
    // Traffic, for takeTraffic and bytesIn/bytesOut. Twelve bytes, set where
    // the activity LED is pulsed, so the lights see exactly what it sees.
    uint16_t  rxSeen_  = 0;          // a bit per Session::id that read bytes
    uint16_t  txSeen_  = 0;          // and that wrote them
    uint32_t  rxBytes_ = 0;
    uint32_t  txBytes_ = 0;
    static_assert(BBS_MAX_NODES + 2 <= 16, "takeTraffic keeps a bit per session in 16");
    Session   nodes_[BBS_MAX_NODES];
    Session   busy_;
    Session   sysop_;
    Session*  all_[kSessions] = {};
    // Per Session::id: when each caller last rang and how often this call.
    // Forgotten in openSession, like everything else a slot might inherit.
    ring::Limiter<kSessions> ringLimits_;
    BanList   bans_;
    LoginGuard logins_;

    // allowed: this session may run this command (permissions, account,
    // plugin levels)
    bool allowed(const Session& s, const Command& c, uint8_t plugin) const;
    uint8_t pluginOf(uint8_t index) const;

    struct CommandTable { const Command* list; uint8_t count; uint8_t plugin; };
    CommandTable tables_[kCommandTables] = {};
    uint8_t      tableCount_ = 0;
};

// sdDashCard: the SD card's free and total space for the dashboard, in KB,
// from the sd plugin's own figure, which it keeps for up to a minute (1.1.0).
// Defined by the plugin and asked by the core, the way sdScreensDir is, so
// the dashboard reads a number rather than the card. False with no card
// mounted or with the plugin switched off.
bool sdDashCard(uint32_t& freeKB, uint32_t& totalKB);
