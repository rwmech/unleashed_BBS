/*
 * File:        src/core/bbs.h
 * Description: BBS core: one listener on the dial-in port, a fixed pool of
 *              caller nodes, a busy line session, a hidden sysop node, and
 *              a single cooperative loop. Sessions are preallocated in
 *              static storage; the loop never allocates.
 *
 *   Sources: bbs.cpp (connections, flow, input, paging),
 *            bbs_shell.cpp (caller commands),
 *            bbs_sysop.cpp (sysop node, co-sysops, staff commands).
 * Listing:     COMPLETE FILE
 * Libraries:   BSD sockets (lwIP on ESP32)
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

enum class SState : uint8_t {
    Free,      // slot unused
    Detect,    // terminal detection
    Intro,     // welcome screens playing
    AskName,   // handle prompt (auth arrives in C2)
    Shell,     // command prompt, or a screen playing inside the shell
    List,      // a paged list (WHO, LAST, NODES...) is being generated
    More,      // "[More]" prompt waiting for a key
    Confirm,   // "Log off (Y/N)?"
    Fx,        // effects demo running
    BusyWait,  // busy line countdown
    Snoop,     // sysop watching another node
    Closing,   // flushing goodbye, then hang up
};

enum class Role : uint8_t {
    Caller,    // nodes 1..BBS_MAX_NODES
    Busy,      // the busy line (all nodes full)
    Sysop,     // hidden sysop node, entered with BYE <password>
};

enum class ListKind : uint8_t { None, Help, Who, Last, Nodes, Bans };
enum class MoreFrom : uint8_t { List, Screen };

struct Session {
    int          fd          = -1;
    uint8_t      id          = 0;      // node number; 0 sysop, BBS_MAX_NODES+1 busy
    SState       st          = SState::Free;
    Role         role        = Role::Caller;
    bool         wantWrite   = false;  // last send would block
    bool         negotiated  = false;  // telnet options sent
    char         ip[16]      = {};
    uint32_t     ipAddr      = 0;      // raw s_addr
    uint32_t     connectedAt = 0;
    uint32_t     lastInput   = 0;
    uint32_t     lastRx      = 0;
    uint32_t     closeAt     = 0;
    uint8_t      fxStep      = 0;
    uint16_t     savedCps    = 0;
    bool         pendingPrompt = false;  // prompt once the screen finishes
    bool         pendingTail   = false;  // hangup tail after goodbye screen
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
    uint32_t     guestUntil  = 0;      // busy-line login deadline

    // paging
    ListKind     list        = ListKind::None;
    uint8_t      listIdx     = 0;
    uint8_t      pageLines   = 0;
    bool         nonstop     = false;
    MoreFrom     moreFrom    = MoreFrom::List;

    // busy line countdown
    uint8_t      countdown   = 0;
    uint32_t     nextTick    = 0;

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

class Bbs {
public:
    static Bbs& instance();

    // begin: open the listener. False if the port cannot be bound.
    bool begin(uint16_t port = BBS_PORT);

    // tick: one scheduler pass (select + service every session)
    void tick();

    uint8_t activeNodes() const;
    static size_t sessionSize() { return sizeof(Session); }

    // key dispatch target (public for the Term callback trampoline)
    void onKey(Session& s, int k, uint32_t now);

private:
    Bbs() = default;

    static constexpr uint8_t kSessions = BBS_MAX_NODES + 2;   // + busy + sysop

    // -- connections (bbs.cpp) ---------------------------------------------
    void acceptAll(uint32_t now);
    void openSession(Session& s, int fd, const char* ip, uint32_t ipAddr, Role role, uint32_t now);
    void closeSession(Session& s, const char* why, uint32_t now);
    void readSession(Session& s, uint32_t now);
    void serviceSession(Session& s, uint32_t now);
    void flush(Session& s, uint32_t now);
    void moveSession(Session& from, Session& to, uint8_t newId, Role role);

    // -- flow (bbs.cpp) -------------------------------------------------------
    void onDetected(Session& s, uint32_t now);
    void startIntro(Session& s);
    void startBusy(Session& s, uint32_t now);
    void askName(Session& s);
    void drawNamePrompt(Session& s);
    void onHandle(Session& s, uint32_t now);
    void prompt(Session& s);
    void drawPrompt(Session& s);
    void hangup(Session& s, const char* msg, uint32_t now);
    void goodbye(Session& s, uint32_t now);
    bool playScreen(Session& s, const char* name);

    // -- timers, notices, paging (bbs.cpp) -----------------------------------
    void checkTimers(Session& s, uint32_t now);
    int32_t secondsLeft(const Session& s, uint32_t now) const;
    bool canNotify(const Session& s) const;
    void notify(Session& s, Color c, const char* msg);
    void redrawInput(Session& s);
    void deliverMail(Session& s);
    void post(Session& to, BusKind kind, const Session* from, const char* text);
    void noticeAll(const Session& about, const char* text);
    void startList(Session& s, ListKind kind);
    void serviceList(Session& s);
    void showMore(Session& s, MoreFrom from);
    void abortOutput(Session& s);
    uint8_t pageRows(const Session& s) const;

    // -- shell (bbs_shell.cpp) -----------------------------------------------
    void runCommand(Session& s, const char* line, uint32_t now);
    // list rows: each call emits exactly one line, false when finished
    bool listRow(Session& s);
    bool rowHelp(Session& s);
    bool rowWho(Session& s);
    bool rowLast(Session& s);
    void cmdHelp(Session& s);
    void cmdMem(Session& s);
    void cmdTerm(Session& s);
    void cmdTime(Session& s, uint32_t now);
    void cmdBaud(Session& s, const char* arg);
    void cmdPage(Session& s, const char* arg);
    void cmdDnd(Session& s);
    void cmdBye(Session& s, const char* arg, uint32_t now);
    void fxNext(Session& s);

    // -- sysop (bbs_sysop.cpp) -----------------------------------------------
    bool runSysop(Session& s, const char* verb, const char* arg, uint32_t now);
    void elevate(Session& s, uint32_t now);
    void coElevate(Session& s, Access level, uint32_t now);
    bool rowNodes(Session& s);
    bool rowBans(Session& s);
    void cmdKick(Session& s, const char* arg, uint32_t now);
    void cmdBroadcast(Session& s, const char* arg);
    void cmdSnoop(Session& s, const char* arg);
    void stopSnoop(Session& s, const char* why);
    void cmdTimeAdjust(Session& s, const char* arg);
    void cmdUnban(Session& s, const char* arg);
    void cmdDrop(Session& s, uint32_t now);
    Session* nodeByArg(const char* arg, const char** rest);

    int       lfd_          = -1;
    uint32_t  heapBaseline_ = 0;
    Session   nodes_[BBS_MAX_NODES];
    Session   busy_;
    Session   sysop_;
    Session*  all_[kSessions] = {};
    BanList   bans_;
    TimeBank  bank_;
};
