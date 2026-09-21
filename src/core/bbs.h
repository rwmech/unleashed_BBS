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
#include "users.h"
#include "form.h"

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
                                Sys, Calls, PlugRows };
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
enum class AfterKey : uint8_t { Prompt, SignupForm, ScreenNext, KnowMore };

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
    bool         newAccount      = false; // first call: show newuser, not bulletin
    uint32_t     heapAtOpen  = 0;
    char         user[BBS_USER_MAX + 1] = {};

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
    MoreFrom     moreFrom    = MoreFrom::List;

    // WHO n / DASH n refresh
    Menu         helpMenu    = Menu::Main;   // which HELP section is being drawn
    bool         helpAll     = false;        // ? all: walk every section in turn
    ListKind     watch       = ListKind::None;
    uint8_t      watchSecs   = 0;
    uint32_t     watchNext   = 0;      // next redraw; 0 = drawing now

    // busy line countdown
    uint8_t      countdown   = 0;
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

    // key dispatch target (public for the Term callback trampoline)
    void onKey(Session& s, int k, uint32_t now);

    // registerCommands: add a command table (kept by pointer, must be
    // static). plugin is the plugin's index, 0xFF for the core table.
    // False when the registry is full.
    bool registerCommands(const Command* list, uint8_t count, uint8_t plugin = 0xFF);
    // dropPluginCommands: take every plugin's table back out, keeping the
    // core's. Called before the plugins are restarted by a config reload.
    void dropPluginCommands();
    // closeCardScreens: end any screen being read from the SD card and hand
    // those callers back to the prompt. Called before the card is unmounted.
    void closeCardScreens();

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
    void rowText(Session& s, Color c, const char* text, bool newline = true);
    void rowRule(Session& s);
    void rowTitle(Session& s, const char* title, const char* right = nullptr);
    uint8_t rowWidth(const Session& s) const;

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
    void flush(Session& s, uint32_t now);
    void moveSession(Session& from, Session& to, uint8_t newId, Role role);

    // -- flow (bbs.cpp) -------------------------------------------------------
    void onDetected(Session& s, uint32_t now);
    void startIntro(Session& s);
    void startBusy(Session& s, uint32_t now);
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
    void cmdInfo(Session& s, const char* arg);
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
    bool playScreen(Session& s, const char* name);

    // -- timers, notices, paging, refresh (bbs.cpp) --------------------------
    void checkTimers(Session& s, uint32_t now);
    int32_t secondsLeft(const Session& s, uint32_t now) const;
    int32_t idleSecondsLeft(const Session& s, uint32_t now) const;
    bool canNotify(const Session& s) const;
    void notify(Session& s, Color c, const char* msg);
    void warnNow(Session& s, const char* msg);
    void redrawInput(Session& s);
    void deliverMail(Session& s);
    void post(Session& to, BusKind kind, const Session* from, const char* text);
    void noticeAll(const Session& about, const char* text);
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
    void rowSection(Session& s, const char* name);
    bool rowSys(Session& s);
    bool rowCalls(Session& s);
    void cmdCalls(Session& s);   // padded when refreshing
    // rowText, rowRule, rowTitle and rowWidth are public: a plugin drawing
    // its own paged list has to produce rows that look like everything else.
    static const char* doingText(const Session& s);

    // -- shell (bbs_shell.cpp) -----------------------------------------------
    void runCommand(Session& s, const char* line, uint32_t now);
    void landAfterLogin(Session& s);
    const Command* findCommand(const char* verb, const Session& s) const;
    const Command* commandAt(uint8_t index) const;
    static const Command* coreCommands(uint8_t& count);
    // list rows: each call emits exactly one line, false when finished
    bool listRow(Session& s);
    bool rowHelp(Session& s);
    bool rowWho(Session& s);
    bool rowLast(Session& s);
    bool rowDash(Session& s);
    // DASH keeps a fixed frame height, so its node block is a fixed number of
    // rows whatever BBS_MAX_NODES is: dashNode picks which session each row
    // shows, busy lines first. See rowDash for why.
    static constexpr uint8_t kDashNodeRows = 6;
    const Session* dashNode(uint8_t slot) const;
    uint8_t dashBusyCount() const;
    bool rowPlugins(Session& s);
    bool rowWatchFooter(Session& s);
    void cmdHelp(Session& s, const char* arg);
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
    void cmdAbout(Session& s);
    void cmdTerm(Session& s);
    void cmdTime(Session& s, const char* arg, uint32_t now);
    void cmdBaud(Session& s, const char* arg);
    void cmdPage(Session& s, const char* arg);
    void cmdDnd(Session& s);
    void cmdBye(Session& s, const char* arg, uint32_t now);
    void fxNext(Session& s);

    // -- sysop (bbs_sysop.cpp) -----------------------------------------------
    void markAccount(Session& s, Access level);
    void elevate(Session& s, uint32_t now);
    static bool localAddr(const char* ip);   // RFC1918, loopback, link-local
    void rememberStaff(const Session& s, Access level);
    void restoreStaff(Session& s);
    void coElevate(Session& s, Access level, uint32_t now);
    bool rowNodes(Session& s);
    bool rowBans(Session& s);
    void cmdKick(Session& s, const char* arg, uint32_t now);
    void cmdBroadcast(Session& s, const char* arg);
    void cmdSnoop(Session& s, const char* arg);
    void stopSnoop(Session& s, const char* why);
    void cmdTimeAdjust(Session& s, const char* arg);

    // unlimited: this call is not on the clock, either because of the
    // caller's rank or because a sysop said so for tonight. Defined in
    // bbs.cpp: can() lives in bbs_util.h, which includes this header.
    bool unlimited(const Session& s) const;
    void cmdUnban(Session& s, const char* arg);
    void cmdDrop(Session& s, uint32_t now);
    void cmdConfig(Session& s, const char* arg, uint32_t now);
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

    int       lfd_          = -1;
    BackupService backup_;
    bool      approvalShown_ = false;
    uint32_t  lastBtnLog_    = 0;
    uint32_t  heapBaseline_ = 0;

    // Running figures for the sysop's system screen. All of them are a few
    // bytes and cost nothing to keep; SYS is where they surface.
    uint32_t  loopAvgUs_   = 0;      // smoothed work per pass, microseconds
    uint32_t  loopMaxUs_   = 0;      // worst pass since boot
    uint32_t  loopPasses_  = 0;      // passes of the scheduler since boot

    // Heap watch. A board that ran out of heap rebooted with nothing said,
    // and the only evidence afterwards was MEM's "heap low since boot"
    // counter having gone UP, which can only happen across a restart. That
    // is a terrible way to find out. These record the floor and say so on
    // the way down, so the next one leaves a trail.
    uint32_t  heapLow_     = 0xFFFFFFFFu;  // lowest free seen, this boot
    uint32_t  heapStep_    = 0;            // lowest threshold already logged
    uint32_t  heapCheckAt_ = 0;            // millis of the last sample
    void      heapWatch(uint32_t now);

    // SHUTDOWN. The board going down on purpose, with everybody told first.
    // shutEnds_ is the millis the countdown reaches zero, and zero means no
    // shutdown is running, which is why it is never legitimately zero while
    // one is: the constructor of that value adds 1 if it lands on zero.
    uint32_t  shutEnds_  = 0;
    uint32_t  shutSaid_  = 0xFFFFFFFFu;    // smallest threshold already announced
    bool      shutDone_  = false;          // the lines are closed, stay closed
    uint16_t  callsBoot_   = 0;      // calls answered since boot
    bool      bootCrash_   = false;  // this boot followed a crash or watchdog
    char      bootReason_[32] = "";  // in words, for the sysop
    uint16_t  bootCrashes_ = 0;      // how many are in the reboot log
    uint8_t   peakNodes_   = 0;      // most nodes busy at once since boot
    uint16_t  callHours_[24] = {};   // CALLS: calls per hour of the day
    uint16_t  callsCounted_ = 0;     // records that went into callHours_
    Session   nodes_[BBS_MAX_NODES];
    Session   busy_;
    Session   sysop_;
    Session*  all_[kSessions] = {};
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
