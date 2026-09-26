/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/announce.cpp
 * Module:       Plugins / announce
 *
 * Purpose:      Tell a directory server this board exists, so callers can
 *               find it. A small heartbeat with the board's name, its
 *               owner and a line of description, sent every few minutes.
 *
 * Design:       This is the one part of the system that talks to the
 *               outside world on its own, so it is off until somebody
 *               switches it on, it sends nothing about callers, and
 *               ANNOUNCE TEST prints the exact bytes it would send before
 *               anybody has to trust it.
 *
 *               The directory records the address the heartbeat arrived
 *               from, which means a board on a home connection with a
 *               changing address stays findable without dynamic DNS. That
 *               is the main reason to run it.
 *
 *               The default directory is the project's own, but the
 *               server list is a setting and the format is documented in
 *               ANNOUNCE.md, so anybody can run one and a board can post
 *               to several at once. A directory nobody can replace would
 *               contradict the rest of this project.
 *
 *                 [plugin:announce]
 *                 enabled     = no
 *                 owner       = Daytona
 *                 description = A BBS on a chip in a shack in Illinois
 *                 servers     = http://unleashedbbs.net/announce
 *                 host        =                  ; a DNS name, if you have one
 *                 public_port =                  ; what callers dial through the router;
 *                                                ; empty: the port the board listens on
 *                 interval    = 10               ; minutes between heartbeats
 *                 token       =                  ; issued by the directory, saved here
 *                 share_activity = no            ; send call counts for ranking
 *                 support     = lgbtq, literacy  ; slugs from the directory's /badges
 *                 interests   = c64, ham         ; the same, hobbies rather than causes
 *
 * Badges:       Six fields describe the board rather than its state (1.0.1).
 *               system, terminals, guests and features are the board's own
 *               facts and are never typed by anybody: the chip and the flash
 *               its image can use, what this firmware speaks, the guest
 *               setting, and which of chat, mail, forums, files and camera
 *               work at the moment of the heartbeat (files, forums and the
 *               camera need a card, and the camera a sensor that answered).
 *               support and interests are the sysop's, as slugs from the
 *               directory's published list; anything it does not know it
 *               ignores, so the board only tidies them, it never judges.
 *
 * Token:        The directory issues one on the first heartbeat and the
 *               board writes it back into its own config, so the listing
 *               survives a reboot and nobody else can claim it. It is not
 *               a spam control: tokens are free to mint, and the thing
 *               that actually costs a spammer is addresses.
 *
 * Network:      Plain HTTP POST, no TLS. Everything in the payload is
 *               public by definition, TLS would cost more heap than the
 *               whole plugin, and a token covers the one thing that
 *               matters, which is somebody else claiming to be your board.
 *
 *               The socket is non-blocking and driven from tick(), so a
 *               directory that is slow or gone never holds up a caller.
 *               The name is looked up on the background runner (1.1.2),
 *               never on the loop, before each round: lwIP answers from its
 *               own cache until the record's TTL runs out, so the network is
 *               asked only when the record says to. The last good address
 *               is kept through a lookup that fails and through a directory
 *               that does not answer, and a name that stops connecting three
 *               times in a row is looked up again. It was a blocking
 *               getaddrinfo on the loop at every start and after every
 *               failed connect: seconds with every caller waiting.
 *
 * Presence:     A caller arriving or leaving (and SHOW, HIDE, LURK, a guest)
 *               makes the directory's count wrong at once, so a heartbeat
 *               goes as soon as the directory allows (1.1.2): the changes of
 *               two seconds are one heartbeat, and it waits no longer than
 *               the gap the directory asks between posts (nudge_seconds, 30
 *               by default, the project directory's own minimum). One that
 *               lands while a post is out is sent after it; a board backing
 *               off after failures still sends it.
 *
 * Libraries:    none (libc, BSD sockets)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     ANNOUNCE.md, PLUGINS.md, PUBLIC.md
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
#include "../core/bbs.h"
#include "../core/bbs_util.h"
#include "../core/plugin.h"
#include "../core/calllog.h"
#include "../core/clock.h"
#include "../core/sysconfig.h"
#include "../core/backup.h"          // sdCardKept: the card's size, for the sd badge
#include "../core/runner.h"         // the name lookup, off the loop (1.1.2)
#include "../platform/platform.h"
#include "chat.h"
#include "camera.h"            // the camera feature, on a camera board
#include "panel_feed.h"       // listing, on a board with a display

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bbsu;

namespace {

constexpr const char kName[]      = "announce";
constexpr uint8_t    kMaxServers  = 4;      // post to this many directories
constexpr uint8_t    kUrlMax      = 96;
constexpr uint8_t    kNameMax     = 40;
// The shortest gap between two heartbeats a caller change may bring about,
// in seconds: the project directory refuses a post from an address that
// posted less than MIN_SECONDS (30) before, and a refused one starts that
// clock again, so going sooner is slower. 32 rather than 30: the board
// times the gap from when its round began and the directory from when the
// post arrived, which is later by the connection's setup, so 30 on the
// board's clock can be 29.9 on the directory's and draw a 429. 0: never
// push on a change.
constexpr uint16_t   kNudgeDef    = 32;
constexpr uint8_t    kRetries     = 3;      // a caller change nobody listed is sent again this often
constexpr uint32_t   kCoalesceMs  = 2000;   // caller changes this close together are one heartbeat
constexpr uint8_t    kReresolve   = 3;      // failed posts in a row before the name is looked up again
constexpr size_t     kTokenMin    = 16;     // shortest token worth believing
constexpr uint8_t    kTokenMax    = 40;
constexpr uint8_t    kDescMax     = 120;
constexpr uint32_t   kTimeoutMs   = 10000;  // a directory has this long to answer
constexpr uint16_t   kIntervalDef = 10;     // minutes

// The two badge lists a sysop types. A list is kept as typed-then-tidied,
// "ham,lgbtq", and never longer than the CONFIG box it is typed into (95
// characters, sizeof(g_cfgBuf[0]) - 1 in bbs_sysop.cpp), so whatever a sysop
// can type is what the board sends. The directory reads 16 entries of a
// list and no slug it publishes is longer than 24 characters (its self-test
// holds every interest to that), so a longer "slug" is junk and is dropped.
constexpr uint8_t    kListMax     = 95;
constexpr uint8_t    kListEntries = 16;
constexpr uint8_t    kSlugMax     = 24;
#ifdef BBS_BOARD_VERSION
// A board profile adds its own version (1.1.0): "ESP32-S3 · 16 MB · PSRAM ·
// S3 1.0.0" is 39 bytes and 35 characters, inside the directory's 40.
constexpr uint8_t    kSystemMax   = 47;
#else
constexpr uint8_t    kSystemMax   = 31;     // "ESP32-S3 · 16 MB · PSRAM" is 26 bytes
#endif

// ---------------------------------------------------------------------------
// The payload's room, and why it is what it is.
//
// A payload that does not fit is refused, never cut (0.21.4): half a JSON
// object is invalid at the far end and the board is quietly delisted. So the
// buffer is sized for the worst payload this code can build, not for a
// typical one, and a refusal can only ever mean a bug.
//
// The worst case, every value at the longest the plugin keeps it and every
// character of every text a '"' or a '\' (each is sent as two bytes):
//
//   keys, quotes, brackets, and the fixed terminals and
//     features lists, with share_activity on                  314
//     (323 on a camera board: ,"camera" is 9 more)
//   numbers at their widest (port 5, nodes 3, busy 3,
//     uptime 7, interval 4, tz 4, calls24 5, minutes24 10)      41
//   version 5 and system 31, never escaped                      36
//   name 40, owner 40, description 120, host 95, token 40,
//     each doubled by escaping                                 670
//   support and interests, 16 entries in 95 characters,
//     quoted and bracketed: 95 + 32 + 2 each                   258
//   ,"sd":1024 while a card is mounted (1.1.0)                  10
//   ,"closed":true while the board is closed (1.1.1)             14
//                                                             -----
//                                                             1,343
//                                     (1,352 on a camera board)
//
// Measured, not only added up: test_announce_badges gives the host board
// exactly this and checks the heartbeat arrives whole. The spare 24 (15 on
// a camera board) is for a longer version string. 768, the figure first proposed, is short even
// with no quote marks anywhere: every text at its longest and both lists
// full is 984.
// ---------------------------------------------------------------------------
constexpr uint16_t   kBodyMax     = 1368;   // 1,343 and the terminator, with room to spare

// The room buildBody writes into: kBodyMax, always, on a board. The host
// build alone lets room_test in [plugin:announce] make it smaller, because
// at kBodyMax nothing the plugin keeps can overflow, which is the point and
// also leaves the refusal path with no way to be tested. Compiled out of
// the firmware, so no system.cfg can shrink a real board's room.
#ifdef BBS_HOST
uint16_t g_room = kBodyMax;
#else
constexpr uint16_t g_room = kBodyMax;
#endif

// The request line and headers. kHeadRoom is computed from this string, so
// the room kept for them cannot drift from what is written into it: four
// conversions of two characters each, replaced by the longest thing each can
// become (a path and a host at kUrlMax - 1, the version, a five digit
// length). sizeof counts the terminator.
#define ANNOUNCE_REQ_FMT                                                      \
    "POST %s HTTP/1.1\r\n"                                                    \
    "Host: %s\r\n"                                                            \
    "User-Agent: unleashed/%s\r\n"                                            \
    "Content-Type: application/json\r\n"                                      \
    "Content-Length: %u\r\n"                                                  \
    "Connection: close\r\n"                                                   \
    "\r\n"
constexpr size_t     kHeadRoom    = sizeof(ANNOUNCE_REQ_FMT) - 4 * 2
                                  + 2 * (kUrlMax - 1) + (sizeof(BBS_VERSION) - 1) + 5;
static_assert(kBodyMax < 10000, "Content-Length is budgeted at four digits and a spare");

// One directory the board posts to, parsed once out of the servers list.
struct Server {
    char     host[kUrlMax] = {};
    char     path[kUrlMax] = {};
    uint16_t port          = 80;
    uint32_t addr          = 0;        // the last good address, 0 never found
    bool     used          = false;
    bool     lookup        = false;    // wants a lookup on the runner
    bool     lookupFailed  = false;    // the last lookup found nothing
    uint8_t  fails         = 0;        // posts in a row that got nowhere
    char     result[48]    = "never sent";
};

Server   g_servers[kMaxServers];
uint8_t  g_count    = 0;
uint8_t  g_index    = 0xFF;

char     g_bbsName[kNameMax + 1] = {};
char     g_owner[kNameMax + 1]   = {};
char     g_desc[kDescMax + 1]    = {};
char     g_host[kUrlMax]         = {};     // what to advertise, empty = our address
char     g_token[kTokenMax + 1]  = {};
char     g_support[kListMax + 1]   = {};   // "ham,lgbtq": tidied, see slugList
char     g_interests[kListMax + 1] = {};
char     g_system[kSystemMax + 1]  = {};   // plat::hardware, once at start
// The port the directory publishes. public_port when the sysop set one,
// because a router may forward a different number to the board; otherwise
// the port the board is listening on right now (Bbs::port), which follows
// the port setting from the restart that makes it true.
uint16_t g_public   = BBS_PORT;
bool     g_publicSet = false;              // public_port is in the file
uint16_t g_interval = kIntervalDef;
uint32_t g_lastRound = 0;                  // when the last round of posts began
uint16_t g_nudgeSecs = kNudgeDef;          // 0 = never push on a caller change
uint32_t g_intervalMs = 0;                 // g_interval in ms (a host test may set it directly)

// A caller change waiting to be sent (1.1.2): the directory's count is wrong
// until it is. g_wantAt is the end of its two second gathering; the round it
// starts takes whatever the count is by then.
bool     g_want     = false;
uint32_t g_wantAt   = 0;
// A round that carried a caller change and listed nowhere (a 429 from the
// directory's per-address limit, which two boards behind one address hit,
// or a directory briefly away) wants the change again after the gap, up to
// kRetries times, rather than leaving it to the next timed heartbeat ten
// minutes on. A new change starts the count again.
bool     g_carry    = false;
uint8_t  g_retry    = 0;
// What a sysop sees in ANNOUNCE (1.1.2): when the last round went, what came
// back, and how the board is backing off after failures.
uint32_t g_lastSentMs    = 0;              // millis the last round began, 0 never
uint32_t g_lastSentEpoch = 0;
char     g_lastResult[48] = "nothing sent yet";
bool     g_roundOk  = false;               // a directory said yes this round
bool     g_roundMiss = false;              // and one did not
bool     g_inRound  = false;
uint8_t  g_backoff  = 0;                   // rounds in a row with nothing listed
uint16_t g_sentBusy = 0, g_sentNodes = 0;  // what the last payload said
uint32_t g_waitAddr = 0;                   // millis a post began waiting on a lookup, 0 not

// ---------------------------------------------------------------------------
// One buffer, three jobs in turn (1.0.1). It holds the payload while it is
// built, then the whole request while it is sent, then the reply while it is
// read. Never two at once: the body is built at kHeadRoom, the headers are
// written in front of it and the body slid down to meet them, the request is
// sent to its last byte before Reading starts, and Reading clears the buffer
// before its first recv. It was three buffers (512 + 768 + 768, 2,048); one
// of kHeadRoom + kBodyMax (1,664) holds a payload two and a half times the
// old limit in less room than the three took.
//
// The clear at the start of Reading is load bearing. startPost used to clear
// the reply before building the request, which was right while they were
// separate buffers and is wrong now: Reading would start with the request in
// the buffer, and the first "nothing yet" found the request's own blank line
// and took it for a complete reply with no bytes in it.
//
// The reply is read across as many passes as it takes. A single recv is
// not a message: TCP will happily hand over half a header, and parsing
// that half gives you half a token, which is exactly what happened.
// ---------------------------------------------------------------------------
char     g_io[kHeadRoom + kBodyMax] = {};
constexpr char* kBody = g_io + kHeadRoom;      // where the payload is built
uint16_t g_replyLen = 0;
bool     g_activity = false;               // send call counts, off unless asked

// The send state machine. One directory at a time; the whole thing is
// driven from tick() so nothing ever blocks the BBS loop.
enum class Stage : uint8_t { Idle, Connecting, Sending, Reading };
Stage    g_stage    = Stage::Idle;
int      g_fd       = -1;
uint8_t  g_at       = 0;               // which server is being posted to
uint32_t g_started  = 0;
uint32_t g_nextRun  = 0;
uint16_t g_sent     = 0;
uint16_t g_bodyLen  = 0;
uint16_t g_reqLen   = 0;
char     g_seenIp[46] = {};            // what the directory says our address is
char     g_state[16]  = {};            // pending, online, offline, queued
uint32_t g_publicIn   = 0;             // seconds until a pending listing shows
uint32_t g_okCount  = 0;
uint32_t g_failCount = 0;
#ifdef BBS_HAS_LCD
// Posts that failed in a row, for the board's display: g_state is the last
// answer a directory gave and is never aged, so without this a board that
// lost its route would show a listed tower all the while it was delisted.
uint8_t  g_failRun  = 0;
#endif

// ---------------------------------------------------------------------------
// parseServer: "http://host:port/path" into its parts. Anything missing
// gets the obvious default, and https is refused rather than pretended at.
// ---------------------------------------------------------------------------
bool parseServer(const char* url, Server& out) {
    out = Server();
    while (*url == ' ') ++url;
    if (!strncasecmp(url, "https://", 8)) {
        plat::log("announce: %s needs TLS, which this plugin does not do", url);
        return false;
    }
    if (!strncasecmp(url, "http://", 7)) url += 7;
    if (!*url) return false;

    size_t n = 0;
    while (*url && *url != ':' && *url != '/' && n + 1 < sizeof(out.host)) out.host[n++] = *url++;
    out.host[n] = '\0';
    if (!out.host[0]) return false;

    if (*url == ':') {
        ++url;
        long p = strtol(url, nullptr, 10);
        if (p < 1 || p > 65535) return false;
        out.port = static_cast<uint16_t>(p);
        while (*url && *url != '/') ++url;
    }
    if (*url == '/') snprintf(out.path, sizeof(out.path), "%.*s", kUrlMax - 1, url);
    else             snprintf(out.path, sizeof(out.path), "/announce");
    out.used = true;
    return true;
}

// readServers: the comma separated list, so one board can be in several
// directories at once
void readServers(const char* value) {
    g_count = 0;
    char list[kUrlMax * kMaxServers];
    snprintf(list, sizeof(list), "%.*s", static_cast<int>(sizeof(list) - 1), value);
    char* save = nullptr;
    for (char* tok = strtok_r(list, ",", &save); tok && g_count < kMaxServers;
         tok = strtok_r(nullptr, ",", &save)) {
        if (parseServer(tok, g_servers[g_count])) ++g_count;
    }
}

// ---------------------------------------------------------------------------
// hasSlug: slug is already one of the entries of a comma list
// ---------------------------------------------------------------------------
bool hasSlug(const char* list, const char* slug, size_t len) {
    for (const char* p = list; *p; ) {
        const char* end = strchr(p, ',');
        size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
        if (n == len && !memcmp(p, slug, len)) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// slugList: what a sysop typed for support or interests, made into what the
// board sends. Each entry is lower cased and keeps only a-z, 0-9 and '-'.
// A run of spaces, underscores or dashes inside it is one '-', and none is
// kept at either end, so " Mental Health " is mental-health, the slug the
// directory publishes, rather than mentalhealth, which matches nothing.
// Anything else is dropped: "<b>c64</b>" is bc64b, which the directory then
// ignores as a word it does not know. Empty entries go, a repeat counts
// once, an entry longer than any slug a directory publishes goes, and the
// first 16 are kept, because 16 is all the directory reads. Whole entries
// only: a list that would outgrow the buffer stops at the last entry that
// fits rather than ending in half a word.
//
// Tidying, not judging. The directory holds the list of what it will show,
// so the board has no list of its own to disagree with it.
// ---------------------------------------------------------------------------
void slugList(const char* in, char* out, size_t n) {
    size_t  w     = 0;
    uint8_t count = 0;
    out[0] = '\0';
    const char* p = in;
    while (*p && count < kListEntries) {
        char   slug[kSlugMax + 1];
        size_t len   = 0;
        bool   long_ = false;
        bool   dash  = false;              // a separator is waiting for a letter
        for (; *p && *p != ','; ++p) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '_' || c == '-') { dash = len > 0; continue; }
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) continue;
            if (dash) {                    // one dash for the whole run, never leading
                if (len < kSlugMax) slug[len++] = '-';
                else long_ = true;
                dash = false;
            }
            if (len < kSlugMax) slug[len++] = c;
            else long_ = true;
        }
        if (*p == ',') ++p;
        if (!len || long_ || hasSlug(out, slug, len)) continue;
        if (w + (w ? 1 : 0) + len + 1 > n) break;        // whole entries only
        if (w) out[w++] = ',';
        memcpy(out + w, slug, len);
        w += len;
        out[w] = '\0';
        ++count;
    }
}

void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
#ifdef BBS_HOST
    if (!strcmp(key, "room_test")) {                  // see g_room
        long v = strtol(value, nullptr, 10);
        if (v >= 64 && v <= kBodyMax) g_room = static_cast<uint16_t>(v);
        return;
    }
#endif
    // "name" used to be read here and would override the board's own. It
    // is gone on purpose: the board is not optional and already has a name,
    // so a second copy in this section could only ever disagree with it.
    // A name left in an old system.cfg is ignored and dropped the next time
    // the file is written.
    if      (!strcmp(key, "owner"))       snprintf(g_owner, sizeof(g_owner), "%.*s", kNameMax, value);
    else if (!strcmp(key, "description")) snprintf(g_desc, sizeof(g_desc), "%.*s", kDescMax, value);
    else if (!strcmp(key, "host"))        snprintf(g_host, sizeof(g_host), "%.*s", kUrlMax - 1, value);
    else if (!strcmp(key, "token")) {
        // Anything shorter than a real token is wreckage from the firmware
        // that truncated them. Start again rather than carry it forward.
        if (strlen(value) >= kTokenMin) snprintf(g_token, sizeof(g_token), "%.40s", value);
        else if (value[0]) plat::log("announce: ignoring a short stored token, re-registering");
    }
    else if (!strcmp(key, "servers"))     readServers(value);
    else if (!strcmp(key, "support"))     slugList(value, g_support, sizeof(g_support));
    else if (!strcmp(key, "interests"))   slugList(value, g_interests, sizeof(g_interests));
    else if (!strcmp(key, "public_port")) {
        // Empty, which is how CONFIG writes a cleared box, leaves the
        // listening port in place: strtol gives 0 and 0 is not a port.
        long p = strtol(value, nullptr, 10);
        if (p >= 1 && p <= 65535) { g_public = static_cast<uint16_t>(p); g_publicSet = true; }
    }
    else if (!strcmp(key, "nudge_seconds")) {
        long v = strtol(value, nullptr, 10);
        if (v >= 0 && v <= 3600) g_nudgeSecs = static_cast<uint16_t>(v);
    }
    else if (!strcmp(key, "interval")) {
        long m = strtol(value, nullptr, 10);
        if (m >= 1 && m <= 1440) { g_interval = static_cast<uint16_t>(m); g_intervalMs = g_interval * 60000u; }
    }
#ifdef BBS_HOST
    // The host alone: the interval in milliseconds, so a test can run a
    // day's heartbeats in minutes against a stand-in directory (1.1.2).
    else if (!strcmp(key, "interval_ms_test")) {
        long v = strtol(value, nullptr, 10);
        if (v >= 50 && v <= 86400000L) g_intervalMs = static_cast<uint32_t>(v);
    }
#endif
    else if (!strcmp(key, "share_activity")) {
        g_activity = !strcasecmp(value, "yes") || !strcasecmp(value, "on") ||
                     !strcasecmp(value, "true") || !strcmp(value, "1");
    }
}

// ---------------------------------------------------------------------------
// Json: the payload is written by hand, straight into the send buffer, one
// piece at a time. Every write is bounded, and one flag says whether any of
// it was lost, which is the only question the caller has.
//
// Writing in place rather than escaping each field into a stack copy and
// then formatting the copies (the 1.0.0 shape) also took about 750 bytes of
// arrays off the stack of whatever called it, on a task whose low water mark
// was 1,440.
// ---------------------------------------------------------------------------
struct Json {
    char*  p;
    size_t cap;                  // bytes, the terminator's included
    size_t len = 0;
    bool   cut = false;

    Json(char* buf, size_t n) : p(buf), cap(n) { p[0] = '\0'; }

    void ch(char c) {
        if (len + 1 < cap) { p[len++] = c; p[len] = '\0'; }
        else cut = true;
    }
    void raw(const char* s) { while (*s) ch(*s++); }
    void num(long v)          { char b[12]; snprintf(b, sizeof(b), "%ld", v); raw(b); }
    void unum(unsigned long v) { char b[12]; snprintf(b, sizeof(b), "%lu", v); raw(b); }

    // str: a JSON string. The two characters that would end it early are
    // escaped, and a control character becomes a space rather than being
    // trusted not to appear. Bytes above 0x7F pass as they are: the board's
    // text is UTF-8, and so is JSON.
    void str(const char* s) {
        ch('"');
        for (; *s; ++s) {
            if (*s == '"' || *s == '\\') { ch('\\'); ch(*s); }
            else if (static_cast<uint8_t>(*s) < 0x20) ch(' ');
            else ch(*s);
        }
        ch('"');
    }

    // list: a comma list as a JSON array of strings. Only ever handed words
    // that need no escaping: slugList's output, or the feature names.
    void list(const char* csv) {
        ch('[');
        for (const char* q = csv; *q; ) {
            if (q != csv) ch(',');
            ch('"');
            while (*q && *q != ',') ch(*q++);
            ch('"');
            if (*q == ',') ++q;
        }
        ch(']');
    }
};

// features: what works at this moment, comma separated, in the order the
// protocol lists them. Never what is merely configured.
//
// A plugin whose files live on the card (PF_SD: forums, files) counts only
// while a card is mounted as well as running, because SD UNMOUNT stops no
// plugin: FORUMS would still be a command, with nothing under it. So an SD
// UNMOUNT takes "forums" and "files" off the next heartbeat. A card pulled
// without SD UNMOUNT is not noticed at all (there is no card-detect line and
// nothing polls the bus), so those two stay until the next boot finds no
// card, the same as everything else on the board that uses it.
//
// "camera" (1.1.0, camera boards only): the camera plugin running with a
// card, and a sensor that answered its latest bring-up this boot (the
// survey's look at start, or a snap). A board whose camera will not start
// does not claim the directory's "This BBS can take pictures" badge.
void features(char* out, size_t n) {
    auto on = [](const char* name) {
        uint8_t i = plugins::indexOf(name);
        const Plugin* p = plugins::at(i);
        return plugins::running(i) && p &&
               (!(p->info.flags & PF_SD) || plat::sdBase()[0]);
    };
    const bool chatOn = on("chat");
#ifdef BBS_HAS_CAMERA
    const bool cam = camera::running() && camera::found();
#else
    const bool cam = false;
#endif
    snprintf(out, n, "%s%s%s%s%s",
             chatOn ? "chat," : "",
             on("forums") ? "forums," : "",
             on("files") ? "files," : "",
             chatOn && chat::mailOn() ? "mail," : "",
             cam ? "camera," : "");
    size_t len = strlen(out);
    if (len) out[len - 1] = '\0';                       // the last comma
}

// ---------------------------------------------------------------------------
// activity: calls and caller-minutes in the last day, from the caller log.
// Counts only. A directory can rank by these so a small board with five
// friends on it outranks a famous dead one, which is the whole point of
// sending them. Off unless the sysop turned share_activity on.
// ---------------------------------------------------------------------------
void activity(uint16_t& calls, uint32_t& minutes) {
    calls   = 0;
    minutes = 0;
    uint32_t nowEpoch = clk::epoch();
    if (!nowEpoch) return;                             // no clock, no honest figures
    uint32_t since = nowEpoch > 86400u ? nowEpoch - 86400u : 0;
    uint8_t  n = calllog::count();
    for (uint8_t i = 0; i < n; ++i) {
        CallRec r;
        if (!calllog::get(i, r) || !r.start || r.start < since) continue;
        ++calls;
        minutes += r.secs / 60u;
    }
}

// ---------------------------------------------------------------------------
// buildBody: what gets sent. Nothing here is about a caller: the board's
// own name, who runs it, how to reach it, and how busy it is. No handles,
// no addresses, no counts of who did what.
// ---------------------------------------------------------------------------
// took: snprintf returns what it WOULD have written, so its return is a
// length only when it is smaller than the buffer. Everywhere else in this
// file it was being stored as one, which is how a truncated payload came to
// advertise a Content-Length past the end of its own array.
//
// Returns the bytes actually in the buffer, and says separately whether
// anything was lost, because those are two different questions and the
// caller needs both.
uint16_t took(int r, size_t cap, bool& cut) {
    if (r < 0) { cut = true; return 0; }
    if (static_cast<size_t>(r) >= cap) { cut = true; return static_cast<uint16_t>(cap - 1); }
    cut = false;
    return static_cast<uint16_t>(r);
}

// buildBody: the payload, at kBody. False when it did not fit, and then
// what is there is a fragment that must not be sent. Sized so that cannot
// happen (see kBodyMax); the check stays because a size argument is only as
// good as the day it was last redone.
bool buildBody() {
    Bbs& bbs = Bbs::instance();
    char feats[40];                                    // "chat,forums,files,mail,camera" is 29
    features(feats, sizeof(feats));

    Json j(kBody, g_room);
    j.raw("{\"software\":\"unleashed\",\"version\":"); j.str(BBS_VERSION);
    j.raw(",\"name\":");        j.str(g_bbsName[0] ? g_bbsName : syscfg::get().hostname);
    j.raw(",\"owner\":");       j.str(g_owner);
    j.raw(",\"description\":"); j.str(g_desc);
    j.raw(",\"host\":");        j.str(g_host);
    j.raw(",\"port\":");        j.unum(g_public);
    g_sentNodes = bbs.publicNodes();
    g_sentBusy  = bbs.publicBusy();
    j.raw(",\"nodes\":");       j.unum(g_sentNodes);
    j.raw(",\"busy\":");        j.unum(g_sentBusy);
    // Closed to callers (1.1.1, Rob): still listed, shown as temporarily
    // closed, rather than dropping off the directory and starting its wait
    // again. Left out while open, as "sd" is while no card is in.
    if (syscfg::get().closed) j.raw(",\"closed\":true");
    j.raw(",\"uptime\":");      j.unum(plat::millis() / 1000u);
    j.raw(",\"interval\":");    j.unum(g_interval);
    j.raw(",\"tz\":");          j.num(clk::utcOffset());
    j.raw(",\"token\":");       j.str(g_token);
    if (g_activity) {
        uint16_t calls = 0;
        uint32_t minutes = 0;
        activity(calls, minutes);
        j.raw(",\"calls24\":");   j.unum(calls);
        j.raw(",\"minutes24\":"); j.unum(minutes);
    }
    // The badges (1.0.1). Every heartbeat carries all six, even an empty
    // list: the directory replaces them on each one, so a field left out is
    // a badge taken down.
    j.raw(",\"system\":");      j.str(g_system);
    j.raw(",\"terminals\":[\"ansi\",\"utf8\",\"petscii\",\"ascii\"]");
    j.raw(",\"guests\":");      j.raw(syscfg::get().guestEnabled ? "true" : "false");
    j.raw(",\"features\":");    j.list(feats);
    j.raw(",\"support\":");     j.list(g_support);
    j.raw(",\"interests\":");   j.list(g_interests);
    // The card's size (1.1.0, PROTOCOL.md "sd"), only while one is mounted:
    // a board whose card is out claims no card. The sd plugin's kept figures,
    // so a heartbeat never touches the card.
    if (uint16_t gb = sdCardGB(sdCardKept())) {       // backup.h
        j.raw(",\"sd\":");      j.unum(gb);
    }
    j.ch('}');

    g_bodyLen = static_cast<uint16_t>(j.len);
    if (j.cut) {
        plat::log("announce: payload is longer than %u bytes, not sending. "
                  "Shorten name, owner, description or host in CONFIG.",
                  static_cast<unsigned>(g_room - 1));
    }
    return !j.cut;
}

// buildRequest: the headers in front of the body, in g_io. They are written
// into the room kept for them at the front and the body slides down to meet
// them. snprintf's terminator lands inside that room, never on the body.
bool buildRequest(const Server& s) {
    g_reqLen = 0;
    if (!buildBody()) return false;
    bool cut = false;
    uint16_t head = took(snprintf(g_io, kHeadRoom, ANNOUNCE_REQ_FMT,
                                  s.path, s.host, BBS_VERSION,
                                  static_cast<unsigned>(g_bodyLen)),
                         kHeadRoom, cut);
    if (cut) {
        plat::log("announce: request headers did not fit %u bytes, not sending. "
                  "The directory path or host is very long.",
                  static_cast<unsigned>(kHeadRoom - 1));
        return false;
    }
    memmove(g_io + head, kBody, g_bodyLen);
    g_reqLen = static_cast<uint16_t>(head + g_bodyLen);
    g_io[g_reqLen] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// The name lookup, on the background runner (1.1.2).
//
// getaddrinfo blocks: lwIP asks each DNS server in turn, four tries apiece
// a second apart, so a lookup that fails is seconds. It ran on the loop at
// every start (every CONFIG save) and after every failed connect, and a
// comment above it said steady state never did, which was not so: a refused
// connect zeroed the address and the next heartbeat looked it up on the loop
// (internal/audit-1.1.2-2026-09-26.md, item 1).
//
// Now the runner asks, one server at a time, and the loop reads the answer
// once the job is done. The address found is kept until a lookup finds
// another: a lookup that fails keeps it, and so does a directory that does
// not answer.
// ---------------------------------------------------------------------------
struct Lookup {
    runner::Job job;
    char        host[kUrlMax] = {};   // in
    uint8_t     server = 0;           // in: which g_servers entry asked
    uint32_t    addr   = 0;           // out
    bool        ok     = false;       // out
};
Lookup g_look;

void lookupWork(runner::Job&) {
    g_look.ok = false;
    g_look.addr = 0;
#ifdef BBS_HOST
    // A test takes the DNS away (hostio.txt's "nodns") to see the board
    // keep its address and come back when it returns.
    if (plat::hostNoDns()) return;
#endif
    addrinfo hints = {};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(g_look.host, nullptr, &hints, &res) != 0 || !res) return;
    g_look.addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    g_look.ok   = g_look.addr != 0;
    freeaddrinfo(res);
}

// lookupTick: collect an answer, and post the next server that wants one.
// Loop only: a load and a compare when there is nothing to do.
void lookupTick() {
    if (runner::done(g_look.job)) {
        const uint8_t i = g_look.server;
        // The server list may have changed under the lookup (a CONFIG save):
        // an answer is only kept for the name it was asked for.
        if (i < g_count && !strcmp(g_servers[i].host, g_look.host)) {
            Server& sv = g_servers[i];
            if (g_look.ok) {
                if (sv.addr != g_look.addr) {
                    const uint8_t* b = reinterpret_cast<const uint8_t*>(&g_look.addr);
                    plat::log("announce: %s is %u.%u.%u.%u", sv.host, b[0], b[1], b[2], b[3]);
                }
                sv.addr = g_look.addr;
                sv.lookupFailed = false;
            } else {
                sv.lookupFailed = true;
                plat::log("announce: cannot find %s%s", sv.host, sv.addr ? "; keeping the last address" : "");
            }
        }
        runner::collect(g_look.job);
    }
    if (!runner::idle(g_look.job)) return;
    for (uint8_t i = 0; i < g_count; ++i) {
        Server& sv = g_servers[i];
        if (!sv.lookup) continue;
        sv.lookup = false;
        snprintf(g_look.host, sizeof(g_look.host), "%s", sv.host);
        g_look.server   = i;
        g_look.job.work = lookupWork;
        g_look.job.name = "announce lookup";
        if (!runner::post(g_look.job)) sv.lookup = true;      // the runner is full: next tick
        return;
    }
}

// askLookup: every server's name, before a round. lwIP answers from its own
// cache while the record's TTL runs, so this asks the network only when the
// record says to, and the round goes on the last address meanwhile.
void askLookup() {
    for (uint8_t i = 0; i < g_count; ++i) g_servers[i].lookup = true;
}

// ---------------------------------------------------------------------------
// header: one header value out of a reply, without a parser. Returns false
// when the header is not there, so a directory that sends none still works.
// ---------------------------------------------------------------------------
bool header(const char* reply, const char* name, char* out, size_t n) {
    out[0] = '\0';
    const char* at = strstr(reply, name);
    if (!at) return false;
    at += strlen(name);
    while (*at == ' ') ++at;
    size_t w = 0;
    while (at[w] && at[w] != '\r' && at[w] != '\n' && w + 1 < n) {
        out[w] = at[w];
        ++w;
    }
    out[w] = '\0';
    return out[0] != '\0';
}

// ---------------------------------------------------------------------------
// saveToken: write the issued token into this plugin's own config section,
// so the board keeps its listing across a reboot. One key, through the same
// writer CONFIG uses, so the rest of system.cfg is left exactly as it was.
// ---------------------------------------------------------------------------
void saveToken() {
    char err[80] = "";
    syscfg::KeyVal pair = { "token", g_token };
    if (syscfg::write(&pair, 1, "plugin:announce", err, sizeof(err)))
        plat::log("announce: listing token saved");
    else
        plat::log("announce: could not save the token: %s", err);
}

// ---------------------------------------------------------------------------
// nudge: somebody arrived or left, so the caller count on the directory is
// now wrong. Bring the next heartbeat forward rather than leaving a board
// advertising "nobody on" for the rest of the interval.
//
// Never sooner than kNudgeGapMs after the last round. Six callers arriving
// together is one update, not six, and a directory that rate limits at
// thirty seconds would refuse the rest anyway.
// ---------------------------------------------------------------------------
// 1.1.2: as soon as the directory allows, and never lost. It returned at
// once while a post was out, so a change that landed during one was never
// sent at all, and it waited sixty seconds after the last round besides:
// the directory ran behind the callers on a live board. Now a change is
// wanted, gathered for two seconds, and sent at the first moment both that
// and the directory's gap allow, after a post in flight if there is one.
// Failures do not hold it: their back-off is for the timed heartbeat.
void nudge(uint32_t now) {
    if (!g_count || !g_nudgeSecs) return;
    g_retry = 0;
    if (!g_want) {
        g_want   = true;
        g_wantAt = now + kCoalesceMs;
    }
}

// The count the directory publishes follows what the outside can see, so
// the nudge hangs on onPresence, which fires for login and logoff and for a
// staff member making themselves visible or invisible. onLogin and onLogoff
// are left off this descriptor rather than doing the same work twice.
void onPresence(Session& s) { (void)s; nudge(plat::millis()); }

// ---------------------------------------------------------------------------
// codeMeans: an HTTP status in words. A sysop looking at a dashboard should
// not have to know what a 429 is to understand that their board is fine and
// simply talking too often.
// ---------------------------------------------------------------------------
const char* codeMeans(int code) {
    switch (code) {
        case 200: return "listed";
        case 400: return "payload refused";
        case 401:
        case 403: return "board refused";
        case 404: return "no directory there";
        case 413: return "payload too large";
        case 429: return "too often, will settle";
        case 500:
        case 502:
        case 503: return "directory busy";
        default:  return "refused";
    }
}

void finish(const char* how, bool ok) {
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    const bool posted = g_stage != Stage::Idle;
    g_stage = Stage::Idle;
    if (g_at < kMaxServers) {
        Server& sv = g_servers[g_at];
        snprintf(sv.result, sizeof(sv.result), "%.39s", how);
        // Every heartbeat's outcome, one line (1.1.2): what a sysop reading
        // the console, or a soak on the bench, looks for.
        plat::log("announce: %s: %s (busy %u of %u)%s", sv.host, how,
                  static_cast<unsigned>(g_sentBusy), static_cast<unsigned>(g_sentNodes),
                  posted ? "" : ", not sent");
        snprintf(g_lastResult, sizeof(g_lastResult), "%.47s", how);
        if (ok) {
            sv.fails = 0;
            g_roundOk = true;
        } else g_roundMiss = true;
        if (!ok && sv.fails < 255 && ++sv.fails >= kReresolve) {
            sv.fails  = 0;
            sv.lookup = true;                       // the address may have moved
        }
    }
    if (ok) ++g_okCount; else ++g_failCount;
#ifdef BBS_HAS_LCD
    g_failRun = ok ? 0 : static_cast<uint8_t>(g_failRun < 255 ? g_failRun + 1 : 255);
#endif
    ++g_at;                                        // next directory on the next tick
}

// startPost: open a non-blocking connection to one directory
void startPost(uint32_t now) {
    Server& s = g_servers[g_at];
    g_replyLen = 0;                            // this exchange starts with nothing
    // A payload that did not fit is not sent at all. Sending the truncated
    // half means invalid JSON at the far end and a board that is quietly not
    // listed, which is worse than an announce that plainly did not happen.
    // Built before connecting, so a refused one opens no connection either:
    // it used to connect first and then close without a byte.
    if (!buildRequest(s)) {
        finish("payload too long", false);
        return;
    }
    // The address comes from the runner (lookupTick). A round never waits
    // on the loop for one: with none found yet it says so and goes on, and
    // the next round has one. A lookup still out is given until the post
    // would have timed out.
    if (!s.addr) {
        if (!g_waitAddr) g_waitAddr = now ? now : 1;
        if (!s.lookupFailed && (s.lookup || !runner::idle(g_look.job)) && now - g_waitAddr < kTimeoutMs)
            return;                                 // try this server again next tick
        char why[48];
        snprintf(why, sizeof(why), "cannot find %.20s", s.host);
        if (!s.lookupFailed && runner::idle(g_look.job)) s.lookup = true;
        g_waitAddr = 0;
        finish(why, false);
        return;
    }
    g_waitAddr = 0;

    g_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_fd < 0) { finish("no socket", false); return; }
    int fl = fcntl(g_fd, F_GETFL, 0);
    fcntl(g_fd, F_SETFL, fl | O_NONBLOCK);

    sockaddr_in a = {};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(s.port);
    a.sin_addr.s_addr = s.addr;
    int r = connect(g_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    if (r < 0 && errno != EINPROGRESS) {
        finish("no route", false);                 // the address is kept (1.1.2)
        return;
    }
    g_sent    = 0;
    g_started = now;
    g_stage   = Stage::Connecting;
}

// ---------------------------------------------------------------------------
// service: one step of the send, called every tick. Each stage does what it
// can without blocking and comes back next time for the rest.
// ---------------------------------------------------------------------------
void service(uint32_t now) {
    if (g_stage == Stage::Idle) return;
    if (now - g_started > kTimeoutMs) { finish("no answer", false); return; }

    if (g_stage == Stage::Connecting) {
        // A socket in the middle of connecting reports SO_ERROR 0, the same
        // as a finished one, so writability has to be the test and the error
        // check only means anything after it. Getting this backwards works
        // on loopback, where connect finishes at once, and fails on a real
        // network, where it never does.
        fd_set w;
        FD_ZERO(&w);
        FD_SET(g_fd, &w);
        timeval tv = { 0, 0 };
        if (select(g_fd + 1, nullptr, &w, nullptr, &tv) <= 0) return;   // still trying

        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(g_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) {
            finish("refused", false);                 // kept; three in a row look it up again
            return;
        }
        g_stage = Stage::Sending;
    }

    if (g_stage == Stage::Sending) {
        while (g_sent < g_reqLen) {
            ssize_t n = send(g_fd, g_io + g_sent, g_reqLen - g_sent, 0);
            if (n > 0) { g_sent = static_cast<uint16_t>(g_sent + n); continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            finish("send failed", false);
            return;
        }
        // The request has gone to its last byte, so the buffer is the
        // reply's now. Cleared here and not earlier: the request is still in
        // it, and it ends in the same blank line Reading looks for.
        g_replyLen = 0;
        g_io[0]    = '\0';
        g_stage = Stage::Reading;
    }

    if (g_stage == Stage::Reading) {
        // Keep reading until the headers are complete, the far end hangs up
        // or the buffer is full. A recv boundary is not a message boundary,
        // and a header cut in half parses as a shorter value rather than as
        // an error: a 32 character token arriving as "1e94" looks perfectly
        // valid, never matches again, and mints a fresh listing on every
        // heartbeat for ever.
        constexpr size_t kReplyMax = sizeof(g_io);
        while (static_cast<size_t>(g_replyLen) + 1 < kReplyMax) {
            ssize_t n = recv(g_fd, g_io + g_replyLen,
                             static_cast<size_t>(kReplyMax - 1 - g_replyLen), 0);
            if (n > 0) {
                g_replyLen = static_cast<uint16_t>(g_replyLen + n);
                g_io[g_replyLen] = '\0';
                if (strstr(g_io, "\r\n\r\n")) break;      // headers are all here
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Nothing more yet. Come back next tick unless what we have
                // is already a complete set of headers.
                if (strstr(g_io, "\r\n\r\n")) break;
                return;
            }
            break;                                  // closed, or an error
        }

        if (!g_replyLen) { finish("no reply", false); return; }
        g_io[g_replyLen] = '\0';
        // Headers that never reached their blank line are not a reply
        // (1.1.2, the reliability suite): a buffer filled, or a connection
        // closed, part way through them, and whatever header was cut reads
        // as a shorter value. A token cut at twenty characters still passes
        // kTokenMin and would be saved over the good one.
        if (!strstr(g_io, "\r\n\r\n")) {
            finish(static_cast<size_t>(g_replyLen) + 1 >= kReplyMax ? "reply too long" : "reply cut short",
                   false);
            return;
        }

        const char* reply = g_io;
        int code = 0;                              // "HTTP/1.1 200 OK"
        if (!strncmp(reply, "HTTP/1.", 7) && g_replyLen > 12) code = atoi(reply + 9);

        header(reply, "X-Seen-Address:", g_seenIp, sizeof(g_seenIp));
        header(reply, "X-Listing-State:", g_state, sizeof(g_state));

        char field[24];
        if (header(reply, "X-Listing-Public-In:", field, sizeof(field)))
            g_publicIn = static_cast<uint32_t>(strtoul(field, nullptr, 10));

        // The directory issues a token the first time and the board keeps it
        // from then on. Saving it is what makes the listing survive a reboot.
        // Only a whole one: a short read must never overwrite a good token
        // with a fragment of a new one.
        char issued[sizeof(g_token)];
        if (header(reply, "X-Listing-Token:", issued, sizeof(issued)) &&
            strlen(issued) >= kTokenMin && strcmp(issued, g_token)) {
            snprintf(g_token, sizeof(g_token), "%.40s", issued);
            saveToken();
        }

        finish(codeMeans(code), code == 200);
    }
}

// roundDone: a round has been through every directory. Timed heartbeats
// back off after a round that listed nowhere, 30 seconds and doubling, up to
// the interval, so a directory that comes back is heard from again in
// minutes rather than at the next interval; a listed round goes back to the
// interval.
void roundDone(uint32_t now) {
    g_inRound = false;
    // The caller change it carried, again once the gap allows, when any
    // directory missed it: with two, one listing it left the other stale
    // until the interval (code review, 1.1.2). The gap is timed from this
    // round's start, which puts the retry just past the directory's 30 s from
    // the refusal it restarted its clock on.
    if (g_carry && g_roundMiss && g_nudgeSecs && g_retry < kRetries && !g_want) {
        ++g_retry;
        g_want   = true;
        g_wantAt = now;
    }
    if (g_roundOk) {
        g_backoff = 0;
        if (!g_roundMiss) g_retry = 0;
        g_nextRun = g_lastSentMs + g_intervalMs;
    } else {
        uint32_t wait = 30000u << (g_backoff < 5 ? g_backoff : 5);
        if (wait > g_intervalMs) wait = g_intervalMs;
        if (g_backoff < 255) ++g_backoff;
        g_nextRun = now + wait;
    }
    if (!g_nextRun) g_nextRun = 1;
}

// beginRound: every directory, in turn, from the next tick.
void beginRound(uint32_t now) {
    g_at          = 0;
    g_inRound     = true;
    g_roundOk     = false;
    g_roundMiss   = false;
    g_carry       = g_want;             // this round carries the change, if one waited
    g_want        = false;              // a change after this is sent by the next round
    g_lastRound   = now;
    g_lastSentMs  = now ? now : 1;
    g_lastSentEpoch = clk::epoch();
    g_waitAddr    = 0;
    g_nextRun     = now + g_intervalMs;  // until the round says otherwise
    askLookup();
}

void tick(uint32_t now) {
    lookupTick();
    if (g_stage != Stage::Idle) { service(now); return; }
    // Held while the sysop password is still the published default (Rob,
    // 1.0.0). A listed board is one strangers will call, and the default is
    // on the install page; the board stays off the list until it is changed.
    // A post already under way finishes above; nothing new starts.
    // A board closed to callers is not held since 1.1.1 (Rob): it goes on
    // sending, with "closed": true, and the directory shows it as
    // temporarily closed. Held, a long close let the listing go stale and
    // start its wait over. The published default above is never listed,
    // closed or not.
    if (syscfg::get().sysopDefault) return;
    if (g_at < g_count) { startPost(now); return; }          // more directories to do
    if (g_inRound) roundDone(now);
    if (!g_count) return;
    // A caller change, gathered and past the directory's gap: now. Not held
    // by a back-off, which is for the timed heartbeat.
    if (g_want && static_cast<int32_t>(now - g_wantAt) >= 0 &&
        (!g_lastSentMs || now - g_lastSentMs >= static_cast<uint32_t>(g_nudgeSecs) * 1000u)) {
        beginRound(now);
        return;
    }
    if (!g_nextRun || static_cast<int32_t>(now - g_nextRun) < 0) return;
    beginRound(now);
}

// ---------------------------------------------------------------------------
// The command. Status by default, NOW to send one immediately, TEST to
// print the exact bytes without sending anything at all.
// ---------------------------------------------------------------------------
void showStatus(Bbs& b, Session& s) {
    char buf[96];
    Term& t = s.term;
    Timeline& tl = s.tl;

    t.color(tl, Color::Cyan);
    t.text(tl, "Announce");
    t.nl(tl);
    if (syscfg::get().sysopDefault) {
        t.color(tl, Color::Yellow);
        t.text(tl, "Held: the sysop password is still the default.");
        t.nl(tl);
        t.text(tl, "CONFIG staff changes it; listing starts after.");
        t.nl(tl);
    } else if (syscfg::get().closed) {
        t.color(tl, Color::Yellow);
        t.text(tl, "Closed: listed as temporarily closed.");
        t.nl(tl);
        t.text(tl, "CONFIG board opens it again.");
        t.nl(tl);
    }
    if (!g_count) {
        t.color(tl, Color::LightRed);
        t.text(tl, "No directory servers configured.");
        t.nl(tl);
    }
    for (uint8_t i = 0; i < g_count; ++i) {
        snprintf(buf, sizeof(buf), "  %.40s%.20s", g_servers[i].host, g_servers[i].path);
        t.color(tl, Color::Grey);
        t.text(tl, buf);
        t.nl(tl);
        snprintf(buf, sizeof(buf), "    %.39s", g_servers[i].result);
        t.color(tl, strstr(g_servers[i].result, "listed") ? Color::LightGreen : Color::Yellow);
        t.text(tl, buf);
        t.nl(tl);
    }
    if (g_state[0]) {
        if (g_publicIn) {
            snprintf(buf, sizeof(buf), "%.15s, public in %uh%02um", g_state,
                     static_cast<unsigned>(g_publicIn / 3600u),
                     static_cast<unsigned>((g_publicIn % 3600u) / 60u));
        } else {
            snprintf(buf, sizeof(buf), "%.15s", g_state);
        }
        t.color(tl, !strcmp(g_state, "online") ? Color::LightGreen : Color::Yellow);
        t.text(tl, buf);
        t.nl(tl);
    }
    snprintf(buf, sizeof(buf), "Every %u min, %u sent, %u failed",
             static_cast<unsigned>(g_interval), static_cast<unsigned>(g_okCount),
             static_cast<unsigned>(g_failCount));
    t.color(tl, Color::Grey);
    t.text(tl, buf);
    t.nl(tl);
    // Alive, and when (1.1.2): the last one sent, what came back, and when
    // the next goes, so a sysop can see it is working without a console.
    {
        const uint32_t now = plat::millis();
        char when[16] = "never";
        if (g_lastSentMs) {
            if (g_lastSentEpoch && clk::valid()) clk::fmtEpoch(when, sizeof(when), "%H:%M:%S", g_lastSentEpoch);
            else snprintf(when, sizeof(when), "%us ago", static_cast<unsigned>((now - g_lastSentMs) / 1000u));
        }
        snprintf(buf, sizeof(buf), "Last sent %s: %.40s", when, g_lastResult);
        t.color(tl, g_roundOk || !g_lastSentMs ? Color::Grey : Color::Yellow);
        t.text(tl, buf);
        t.nl(tl);
        if (syscfg::get().sysopDefault) {
            snprintf(buf, sizeof(buf), "Next: held");
        } else if (g_stage != Stage::Idle || (g_inRound && g_at < g_count)) {
            snprintf(buf, sizeof(buf), "Next: going out now");
        } else {
            uint32_t next = g_nextRun;
            if (g_want) {
                uint32_t w = g_wantAt;
                const uint32_t gap = g_lastSentMs + static_cast<uint32_t>(g_nudgeSecs) * 1000u;
                if (g_lastSentMs && static_cast<int32_t>(gap - w) > 0) w = gap;
                if (!next || static_cast<int32_t>(w - next) < 0) next = w;
            }
            const int32_t in = next ? static_cast<int32_t>(next - now) : 0;
            const uint32_t secs = in > 0 ? static_cast<uint32_t>(in) / 1000u : 0;
            snprintf(buf, sizeof(buf), "Next in %um %02us%s%s", static_cast<unsigned>(secs / 60u),
                     static_cast<unsigned>(secs % 60u), g_want ? ", a caller change" : "",
                     g_backoff ? ", backing off" : "");
        }
        t.text(tl, buf);
        t.nl(tl);
    }
    if (g_activity) {
        uint16_t calls = 0;
        uint32_t minutes = 0;
        activity(calls, minutes);
        snprintf(buf, sizeof(buf), "Sharing activity: %u calls, %u caller-min",
                 static_cast<unsigned>(calls), static_cast<unsigned>(minutes));
        t.color(tl, Color::DarkGrey);
        t.text(tl, buf);
        t.nl(tl);
    }
    if (g_seenIp[0]) {
        snprintf(buf, sizeof(buf), "Seen from outside as %.40s", g_seenIp);
        t.color(tl, Color::LightGreen);
        t.text(tl, buf);
        t.nl(tl);
    }
    b.prompt(s);
}

void showPayload(Bbs& b, Session& s) {
    // The payload is built in the buffer a heartbeat is sent and answered
    // from, so not while one is out. On a LAN that is well under a second;
    // the longest it can be is kTimeoutMs.
    if (g_stage != Stage::Idle) {
        s.term.color(s.tl, Color::Yellow);
        s.term.text(s.tl, "A heartbeat is going out. TEST again in a moment.");
        b.prompt(s);
        return;
    }
    if (!buildBody()) {
        // What is in the buffer is a fragment, and printing it as "what
        // leaves the board" would be a lie: nothing leaves at all.
        s.term.color(s.tl, Color::LightRed);
        s.term.text(s.tl, "The payload does not fit its room.");
        s.term.nl(s.tl);
        s.term.text(s.tl, "It would be refused, nothing sent.");
        s.term.nl(s.tl);
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "Shorten a field in CONFIG announce.");
        b.prompt(s);
        return;
    }
    s.term.color(s.tl, Color::DarkGrey);
    s.term.text(s.tl, "This is everything that leaves the board:");
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::White);
    s.term.text(s.tl, kBody);
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::DarkGrey);
    s.term.text(s.tl, "Nothing about callers is in it, ever.");
    s.term.nl(s.tl);
    b.prompt(s);
}

const Command kCommands[] = {
    { "ANNOUNCE", "", 0, CF_ADMIN, "ANNOUNCE", "directory listing status",
      [](Bbs& b, Session& s, const char* a, uint32_t now) {
          if (ieq(a, "test"))  { showPayload(b, s); return; }
          if (ieq(a, "now")) {
              // Start a round now, and put the timer where it belongs. Setting
              // it to now as well makes the timer fire the instant the round
              // finishes, so every manual send went out twice.
              if (g_stage == Stage::Idle && !(g_inRound && g_at < g_count)) beginRound(now);
              s.term.color(s.tl, Color::LightGreen);
              s.term.text(s.tl, "Sending now.");
              b.prompt(s);
              return;
          }
          showStatus(b, s);
      },
      Menu::Sysop, 4 },
    { "ANNOUNCE", "", 0, CF_ADMIN | CF_HELPONLY, "ANNOUNCE TEST", "print the payload, send nothing", nullptr,
      Menu::Sysop, 5 },
    { "ANNOUNCE", "", 0, CF_ADMIN | CF_HELPONLY, "ANNOUNCE NOW", "send a heartbeat now", nullptr,
      Menu::Sysop, 6 },
};

bool start(Bbs& bbs) {
    g_index = plugins::indexOf(kName);
    g_count = 0;
    g_at    = 0xFF;
    g_stage = Stage::Idle;
    g_bbsName[0] = g_owner[0] = g_desc[0] = g_host[0] = g_token[0] = '\0';
    g_support[0] = g_interests[0] = '\0';
#ifdef BBS_HOST
    g_room = kBodyMax;                     // room_test lasts one config, not for ever
#endif
    // What the board is: the chip, and the flash this image can use. Once,
    // here, because neither can change while the board is running.
    plat::hardware(g_system, sizeof(g_system));
#ifdef BBS_BOARD_VERSION
    // And a board profile's own version (1.1.0), as one more part of the
    // same badge: `version` stays the core's, which is what the directory
    // compares for its update arrow.
    {
        const size_t at = strlen(g_system);
        snprintf(g_system + at, sizeof(g_system) - at, " \xC2\xB7 %s %s", BBS_BOARD_TAG, BBS_BOARD_VERSION);
    }
#endif
    g_seenIp[0]  = '\0';
    g_state[0]   = '\0';
    g_publicIn   = 0;
    g_activity   = false;
    g_nudgeSecs  = kNudgeDef;
    g_intervalMs = static_cast<uint32_t>(kIntervalDef) * 60000u;
    // The listener is up before any plugin starts (bbsTask, and a CONFIG
    // save restarts the plugins with it still bound), so this is the port
    // callers reach right now. BBS_PORT only if that ever stops being true.
    g_public     = bbs.port() ? bbs.port() : BBS_PORT;
    g_publicSet  = false;
    g_interval   = kIntervalDef;
    // The board's name, and the only place it comes from. This used to be
    // a seed that the plugin's own "name" key could then override, which
    // meant two CONFIG pages each offering a field called "Board" and no
    // way for a sysop to tell which one the directory would publish.
    snprintf(g_bbsName, sizeof(g_bbsName), "%.*s", kNameMax, syscfg::get().boardName);
    readServers("http://unleashedbbs.net/announce");         // the default, replaceable
    plugins::forEachKey(g_index, readKey, nullptr);

    // The names are looked up on the runner (1.1.2): this was a blocking
    // lookup of every server right here, and start runs at every CONFIG save.
    // The first round goes at once and waits on the lookup for up to the
    // post's own timeout.
    g_want     = false;
    g_carry    = false;
    g_retry    = 0;
    g_inRound  = false;
    g_backoff  = 0;
    beginRound(plat::millis());
    plat::log("announce: %u director%s, every %u min", static_cast<unsigned>(g_count),
              g_count == 1 ? "y" : "ies", static_cast<unsigned>(g_interval));
    return true;
}

// ---------------------------------------------------------------------------
// status: one line for the sysop dashboard. Whether this board is listed,
// and while it is still earning its listing, how long is left.
// ---------------------------------------------------------------------------
const char* status() {
    static char line[64];
    if (!g_count) return nullptr;
    if (syscfg::get().sysopDefault) return "Directory: held, the sysop password is the default";
    if (syscfg::get().closed)       return "Directory: listed as temporarily closed";
    if (g_state[0] && g_publicIn) {
        snprintf(line, sizeof(line), "Directory: %.8s, public in %uh%02um  %u sent", g_state,
                 static_cast<unsigned>(g_publicIn / 3600u),
                 static_cast<unsigned>((g_publicIn % 3600u) / 60u),
                 static_cast<unsigned>(g_okCount));
    } else if (g_state[0]) {
        snprintf(line, sizeof(line), "Directory: %.10s  %u sent %u failed", g_state,
                 static_cast<unsigned>(g_okCount), static_cast<unsigned>(g_failCount));
    } else {
        snprintf(line, sizeof(line), "Directory: %.34s", g_servers[0].result);
    }
    return line;
}

#ifdef BBS_HAS_LCD
}   // namespace

// The board's display's tower (panel_feed.h), from the same figures status()
// says in words. "pending" and "queued" wait with "held": a new listing is
// hours from public by design, and a red tower for all of them would teach a
// sysop to ignore the red one that matters.
uint8_t announce::listing() {
    if (!g_count) return 0;
    if (syscfg::get().sysopDefault || syscfg::get().closed) return 2;
    // Two posts in a row that got nowhere: whatever the last answer said,
    // the board is not being heard. Two, so one dropped post does not
    // flicker the tower.
    if (g_failRun >= 2) return 3;
    if (!g_state[0]) return g_failCount ? 3 : 0;
    if (!strcmp(g_state, "online")) return 1;
    if (!strcmp(g_state, "held") || !strcmp(g_state, "pending") || !strcmp(g_state, "queued")) return 2;
    return 3;
}

namespace {
#endif

void stop() {
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    g_stage = Stage::Idle;
    g_at    = 0xFF;
    g_inRound = false;
    // A lookup still out answers into g_look; lookupTick keeps it only for
    // a name the new list still has.
}

// ---------------------------------------------------------------------------
// What CONFIG offers. Declaring these is what makes them reachable: before,
// a plugin's page was built from whatever keys system.cfg already carried,
// so a board that had never written "description" had no way to set one
// short of editing the file by hand.
//
// Labels are nine characters, the width of the form's left column at 40,
// and each has a twenty character one for 80 (1.1.0).
// ---------------------------------------------------------------------------
const PluginSetting kSettings[] = {
    // Shown, not editable. The board's name lives in board_name on the core
    // Board page, which every board has whether or not it announces, and
    // this is the value that gets published. Showing it here answers "what
    // will the directory call me" without creating a second copy that can
    // disagree with the first.
    { "name",           "Board",     PS_INFO,  0, 0,     kNameMax,    nullptr, nullptr, "Board name" },
    { "owner",          "Sysop",     PS_TEXT,  0, 0,     kNameMax,    nullptr, nullptr, "Sysop name" },
    { "description",    "About",     PS_TEXT,  0, 0,     kDescMax,    nullptr, nullptr, "Description" },
    // Empty means "advertise whatever address the directory saw". A board on
    // a name of its own puts it here: quantum.dnsfor.me, say.
    { "host",           "DNS name",  PS_TEXT,  0, 0,     kUrlMax - 1, nullptr, nullptr, "Public DNS name" },
    // The router's side of a port forward. "Outside port" is twelve
    // characters against a nine character label column, so the note carries
    // the word at 40; at 80 the label says it. Empty publishes the board's
    // own port, which is right for every router that forwards the same
    // number it receives.
    { "public_port",    "Outside",   PS_OPTNUM, 1, 65535, 5,
      "What callers dial through your router.", nullptr, "Outside port",
      "The router's outside port, when it differs. Blank publishes the board's own." },
    // Comma separated, so one board can be listed in several directories.
    { "servers",        "Directory", PS_TEXT,  0, 0,     90,          nullptr, nullptr, "Directory URLs" },
    { "interval",       "Every min", PS_NUM,   1, 1440,  4,           nullptr, nullptr, "Heartbeat minutes" },
    // A caller arriving makes the directory's count wrong at once, so the
    // board pushes an update. This is the shortest gap between pushes;
    // 0 turns them off and leaves only the timed heartbeat.
    { "nudge_seconds",  "Push secs", PS_NUM,   0, 3600,  4,           nullptr, nullptr, "Push gap, seconds" },
    { "share_activity", "Activity",  PS_YESNO, 0, 0,     4,           nullptr, nullptr, "Share activity" },
    // The sysop's badges (1.0.1): comma lists of slugs copied from the
    // directory's /badges page. Tidied on the way out, see slugList.
    { "support",        "Support",   PS_TEXT,  0, 0,     kListMax,    nullptr, nullptr, "Support badges" },
    { "interests",      "Interests", PS_TEXT,  0, 0,     kListMax,    nullptr, nullptr, "Interest badges" },
    // Issued by the directory and kept so a listing survives a reflash.
    { "token",          "Token",     PS_TEXT,  0, 0,     kTokenMax,   nullptr, nullptr, "Directory token" },
};
// The rows the core puts first (kCoreRows: enabled, read, write, admin) and
// these fill a CONFIG page: one more and CONFIG drops the last silently.
static_assert(kCoreRows + sizeof(kSettings) / sizeof(kSettings[0]) <= Form::kMaxFields,
              "announce's CONFIG page is full");
// And CONFIG has to hold the longest value this plugin takes, or saving the
// row cuts it: it held 95 of the description's 120 until 1.1.0.
static_assert(kDescMax <= kSettingMax && kListMax <= kSettingMax && kUrlMax - 1 <= kSettingMax,
              "a setting is longer than CONFIG can hold: raise kSettingMax");

// ---------------------------------------------------------------------------
// setting: what this plugin is running with, for a key system.cfg has not
// been given yet. A blank on the form should mean "not set", never "set to
// something I cannot show you".
// ---------------------------------------------------------------------------
void setting(const char* key, char* out, size_t n) {
    if      (!strcmp(key, "name"))        snprintf(out, n, "%s", g_bbsName);
    else if (!strcmp(key, "owner"))       snprintf(out, n, "%s", g_owner);
    else if (!strcmp(key, "description")) snprintf(out, n, "%s", g_desc);
    else if (!strcmp(key, "host"))        snprintf(out, n, "%s", g_host);
    else if (!strcmp(key, "token"))       snprintf(out, n, "%s", g_token);
    else if (!strcmp(key, "support"))     snprintf(out, n, "%s", g_support);
    else if (!strcmp(key, "interests"))   snprintf(out, n, "%s", g_interests);
    // Blank until the sysop sets one: blank is the setting that follows the
    // board's own port, and showing that number here would invite a save
    // that pins it.
    else if (!strcmp(key, "public_port")) {
        if (g_publicSet) snprintf(out, n, "%u", static_cast<unsigned>(g_public));
        else             out[0] = '\0';
    }
    else if (!strcmp(key, "interval"))    snprintf(out, n, "%u", static_cast<unsigned>(g_interval));
    else if (!strcmp(key, "nudge_seconds")) snprintf(out, n, "%u", static_cast<unsigned>(g_nudgeSecs));
    else if (!strcmp(key, "share_activity")) snprintf(out, n, "%s", g_activity ? "yes" : "no");
    else if (!strcmp(key, "servers")) {
        // Rebuilt from the parsed list rather than echoed, so what the form
        // shows is what the board will actually post to.
        size_t used = 0;
        out[0] = '\0';
        for (uint8_t i = 0; i < g_count && used + 1 < n; ++i) {
            int w = snprintf(out + used, n - used, "%s%s://%s%s",
                             used ? "," : "", "http", g_servers[i].host, g_servers[i].path);
            if (w < 0) break;
            used += static_cast<size_t>(w);
        }
    }
    else out[0] = '\0';
}

} // namespace

extern const Plugin kAnnouncePlugin = {
    // off until somebody switches it on: this is the one thing that talks out
    { kName, "Directory listing", "1.0", 0, 0, PF_CORE, PlugLevel::Sysop, PlugLevel::Sysop, PlugLevel::Sysop },
    start,
    stop,
    tick,
    nullptr,                 // onConnect
    nullptr,                 // onLogin: see onPresence
    nullptr,                 // onLogoff: see onPresence
    nullptr,                 // onKey
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    sizeof(kSettings) / sizeof(kSettings[0]),
    setting,
    nullptr,                 // rows: no paged list of its own
    onPresence,              // arrivals, departures and SHOW/HIDE/LURK
    nullptr,                 // onBytes
    nullptr,                 // onRename
    nullptr,                 // listDone
};
