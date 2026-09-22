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
 *                 public_port = 6400             ; the port callers dial
 *                 interval    = 10               ; minutes between heartbeats
 *                 token       =                  ; issued by the directory, saved here
 *                 share_activity = no            ; send call counts for ranking
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
 *               The address is resolved once at start, when nobody is
 *               connected, and only looked up again after a failure.
 *
 * Libraries:    none (libc, BSD sockets)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     ANNOUNCE.md, PLUGINS.md, PUBLIC.md
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
#include "../core/calllog.h"
#include "../core/clock.h"
#include "../core/sysconfig.h"
#include "../platform/platform.h"

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
constexpr uint16_t   kNudgeDef    = 60;     // seconds: shortest gap a caller change may force
constexpr uint16_t   kReplyMax    = 768;    // enough for the status line, headers and a small body
constexpr size_t     kTokenMin    = 16;     // shortest token worth believing
constexpr uint8_t    kDescMax     = 120;
// 512 is NOT comfortably above the worst case, whatever the comment here
// used to say. name, owner, description, host and token are CONFIG values
// and a CONFIG value buffer is 96 bytes, so five of them is 475 characters
// before any JSON skeleton: a fully described board needs roughly 700. The
// buffer stays 512 because that is the size the protocol documents and a
// board does not need a 700 character description; what changed is that
// overflowing it is now detected and refused rather than silently sent.
constexpr uint16_t   kBodyMax     = 512;
constexpr uint32_t   kTimeoutMs   = 10000;  // a directory has this long to answer
constexpr uint16_t   kIntervalDef = 10;     // minutes

// One directory the board posts to, parsed once out of the servers list.
struct Server {
    char     host[kUrlMax] = {};
    char     path[kUrlMax] = {};
    uint16_t port          = 80;
    uint32_t addr          = 0;        // resolved, 0 = needs a lookup
    bool     used          = false;
    char     result[48]    = "never sent";
};

Server   g_servers[kMaxServers];
uint8_t  g_count    = 0;
uint8_t  g_index    = 0xFF;

char     g_bbsName[kNameMax + 1] = {};
char     g_owner[kNameMax + 1]   = {};
char     g_desc[kDescMax + 1]    = {};
char     g_host[kUrlMax]         = {};     // what to advertise, empty = our address
char     g_token[41]             = {};
uint16_t g_public   = BBS_PORT;
uint16_t g_interval = kIntervalDef;
uint32_t g_lastRound = 0;                  // when the last round of posts began
uint16_t g_nudgeSecs = kNudgeDef;          // 0 = never push on a caller change

// The reply is read across as many passes as it takes. A single recv is
// not a message: TCP will happily hand over half a header, and parsing
// that half gives you half a token, which is exactly what happened.
char     g_reply[kReplyMax] = {};
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
char     g_body[kBodyMax] = {};
uint16_t g_bodyLen  = 0;
char     g_request[kBodyMax + 256] = {};
uint16_t g_reqLen   = 0;
char     g_seenIp[46] = {};            // what the directory says our address is
char     g_state[16]  = {};            // pending, online, offline, queued
uint32_t g_publicIn   = 0;             // seconds until a pending listing shows
uint32_t g_okCount  = 0;
uint32_t g_failCount = 0;

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

void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
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
    else if (!strcmp(key, "public_port")) {
        long p = strtol(value, nullptr, 10);
        if (p >= 1 && p <= 65535) g_public = static_cast<uint16_t>(p);
    }
    else if (!strcmp(key, "nudge_seconds")) {
        long v = strtol(value, nullptr, 10);
        if (v >= 0 && v <= 3600) g_nudgeSecs = static_cast<uint16_t>(v);
    }
    else if (!strcmp(key, "interval")) {
        long m = strtol(value, nullptr, 10);
        if (m >= 1 && m <= 1440) g_interval = static_cast<uint16_t>(m);
    }
    else if (!strcmp(key, "share_activity")) {
        g_activity = !strcasecmp(value, "yes") || !strcasecmp(value, "on") ||
                     !strcasecmp(value, "true") || !strcmp(value, "1");
    }
}

// ---------------------------------------------------------------------------
// jsonEscape: the payload is written by hand, so the few characters that
// would break it are escaped here rather than trusted not to appear.
// ---------------------------------------------------------------------------
void jsonEscape(const char* in, char* out, size_t n) {
    size_t w = 0;
    for (const char* p = in; *p && w + 2 < n; ++p) {
        if (*p == '"' || *p == '\\') { out[w++] = '\\'; out[w++] = *p; }
        else if (static_cast<uint8_t>(*p) < 0x20) out[w++] = ' ';
        else out[w++] = *p;
    }
    out[w < n ? w : n - 1] = '\0';
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

// buildBody: false when the payload did not fit, and then g_body holds a
// truncated fragment that must not be sent.
bool buildBody() {
    char name[kNameMax * 2 + 2], owner[kNameMax * 2 + 2], desc[kDescMax * 2 + 2];
    char host[kUrlMax * 2], token[84];
    jsonEscape(g_bbsName[0] ? g_bbsName : syscfg::get().hostname, name, sizeof(name));
    jsonEscape(g_owner, owner, sizeof(owner));
    jsonEscape(g_desc, desc, sizeof(desc));
    jsonEscape(g_host, host, sizeof(host));
    jsonEscape(g_token, token, sizeof(token));

    Bbs& bbs = Bbs::instance();
    char extra[64] = "";
    if (g_activity) {
        uint16_t calls = 0;
        uint32_t minutes = 0;
        activity(calls, minutes);
        snprintf(extra, sizeof(extra), ",\"calls24\":%u,\"minutes24\":%u",
                 static_cast<unsigned>(calls), static_cast<unsigned>(minutes));
    }
    bool cut = false;
    g_bodyLen = took(snprintf(g_body, sizeof(g_body),
        "{\"software\":\"unleashed\",\"version\":\"%s\","
        "\"name\":\"%s\",\"owner\":\"%s\",\"description\":\"%s\","
        "\"host\":\"%s\",\"port\":%u,\"nodes\":%u,\"busy\":%u,"
        "\"uptime\":%u,\"interval\":%u,\"tz\":%d,\"token\":\"%s\"%s}",
        BBS_VERSION, name, owner, desc, host,
        static_cast<unsigned>(g_public),
        static_cast<unsigned>(bbs.publicNodes()),
        static_cast<unsigned>(bbs.publicBusy()),
        static_cast<unsigned>(plat::millis() / 1000u),
        static_cast<unsigned>(g_interval),
        static_cast<int>(clk::utcOffset()),
        token, extra), sizeof(g_body), cut);
    if (cut) {
        plat::log("announce: payload is longer than %u bytes, not sending. "
                  "Shorten name, owner, description or host in CONFIG.",
                  static_cast<unsigned>(sizeof(g_body) - 1));
    }
    return !cut;
}

bool buildRequest(const Server& s) {
    if (!buildBody()) { g_reqLen = 0; return false; }
    bool cut = false;
    g_reqLen = took(snprintf(g_request, sizeof(g_request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: unleashed/%s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n%s",
        s.path, s.host, BBS_VERSION, static_cast<unsigned>(g_bodyLen), g_body),
        sizeof(g_request), cut);
    if (cut) {
        plat::log("announce: request did not fit %u bytes, not sending. "
                  "The directory path or host is very long.",
                  static_cast<unsigned>(sizeof(g_request) - 1));
        g_reqLen = 0;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// resolve: one blocking lookup, done when the board is quiet. Steady state
// never resolves, so a slow DNS server cannot stall a caller mid sentence.
// ---------------------------------------------------------------------------
bool resolve(Server& s) {
    addrinfo hints = {};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(s.host, nullptr, &hints, &res) != 0 || !res) {
        snprintf(s.result, sizeof(s.result), "cannot find %.20s", s.host);
        return false;
    }
    s.addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    return true;
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
void nudge(uint32_t now) {
    if (!g_count || !g_nudgeSecs || g_stage != Stage::Idle) return;
    uint32_t soonest = g_lastRound + static_cast<uint32_t>(g_nudgeSecs) * 1000u;
    if (static_cast<int32_t>(now - soonest) >= 0) soonest = now;   // already allowed
    if (static_cast<int32_t>(soonest - g_nextRun) < 0) g_nextRun = soonest;
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
    g_stage = Stage::Idle;
    if (g_at < kMaxServers) snprintf(g_servers[g_at].result, sizeof(g_servers[g_at].result), "%.39s", how);
    if (ok) ++g_okCount; else ++g_failCount;
    ++g_at;                                        // next directory on the next tick
}

// startPost: open a non-blocking connection to one directory
void startPost(uint32_t now) {
    Server& s = g_servers[g_at];
    g_replyLen = 0;                            // this exchange starts with nothing
    g_reply[0] = '\0';
    if (!s.addr && !resolve(s)) { finish(s.result, false); return; }

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
        s.addr = 0;                                // look it up again next time
        finish("no route", false);
        return;
    }
    // A payload that did not fit is not sent at all. Sending the truncated
    // half means invalid JSON at the far end and a board that is quietly not
    // listed, which is worse than an announce that plainly did not happen.
    if (!buildRequest(s)) {
        finish("payload too long", false);
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
            g_servers[g_at].addr = 0;                 // look the name up again next time
            finish("refused", false);
            return;
        }
        g_stage = Stage::Sending;
    }

    if (g_stage == Stage::Sending) {
        while (g_sent < g_reqLen) {
            ssize_t n = send(g_fd, g_request + g_sent, g_reqLen - g_sent, 0);
            if (n > 0) { g_sent = static_cast<uint16_t>(g_sent + n); continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            finish("send failed", false);
            return;
        }
        g_stage = Stage::Reading;
    }

    if (g_stage == Stage::Reading) {
        // Keep reading until the headers are complete, the far end hangs up
        // or the buffer is full. A recv boundary is not a message boundary,
        // and a header cut in half parses as a shorter value rather than as
        // an error: a 32 character token arriving as "1e94" looks perfectly
        // valid, never matches again, and mints a fresh listing on every
        // heartbeat for ever.
        while (g_replyLen + 1 < kReplyMax) {
            ssize_t n = recv(g_fd, g_reply + g_replyLen,
                             static_cast<size_t>(kReplyMax - 1 - g_replyLen), 0);
            if (n > 0) {
                g_replyLen = static_cast<uint16_t>(g_replyLen + n);
                g_reply[g_replyLen] = '\0';
                if (strstr(g_reply, "\r\n\r\n")) break;   // headers are all here
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Nothing more yet. Come back next tick unless what we have
                // is already a complete set of headers.
                if (strstr(g_reply, "\r\n\r\n")) break;
                return;
            }
            break;                                  // closed, or an error
        }

        if (!g_replyLen) { finish("no reply", false); return; }
        g_reply[g_replyLen] = '\0';

        int code = 0;                              // "HTTP/1.1 200 OK"
        if (!strncmp(g_reply, "HTTP/1.", 7) && g_replyLen > 12) code = atoi(g_reply + 9);

        header(g_reply, "X-Seen-Address:", g_seenIp, sizeof(g_seenIp));
        header(g_reply, "X-Listing-State:", g_state, sizeof(g_state));

        char field[24];
        if (header(g_reply, "X-Listing-Public-In:", field, sizeof(field)))
            g_publicIn = static_cast<uint32_t>(strtoul(field, nullptr, 10));

        // The directory issues a token the first time and the board keeps it
        // from then on. Saving it is what makes the listing survive a reboot.
        // Only a whole one: a short read must never overwrite a good token
        // with a fragment of a new one.
        char issued[sizeof(g_token)];
        if (header(g_reply, "X-Listing-Token:", issued, sizeof(issued)) &&
            strlen(issued) >= kTokenMin && strcmp(issued, g_token)) {
            snprintf(g_token, sizeof(g_token), "%.40s", issued);
            saveToken();
        }

        finish(codeMeans(code), code == 200);
    }
}

void tick(uint32_t now) {
    if (g_stage != Stage::Idle) { service(now); return; }
    if (g_at < g_count) { startPost(now); return; }          // more directories to do
    if (!g_count || !g_nextRun) return;
    if (static_cast<int32_t>(now - g_nextRun) < 0) return;
    g_nextRun  = now + static_cast<uint32_t>(g_interval) * 60000u;
    g_lastRound = now;
    g_at       = 0;                                          // round again
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
    buildBody();
    s.term.color(s.tl, Color::DarkGrey);
    s.term.text(s.tl, "This is everything that leaves the board:");
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::White);
    s.term.text(s.tl, g_body);
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
              g_at      = 0;
              g_nextRun = now + static_cast<uint32_t>(g_interval) * 60000u;
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
    (void)bbs;
    g_index = plugins::indexOf(kName);
    g_count = 0;
    g_at    = 0xFF;
    g_stage = Stage::Idle;
    g_bbsName[0] = g_owner[0] = g_desc[0] = g_host[0] = g_token[0] = '\0';
    g_seenIp[0]  = '\0';
    g_state[0]   = '\0';
    g_publicIn   = 0;
    g_activity   = false;
    g_nudgeSecs  = kNudgeDef;
    g_public     = BBS_PORT;
    g_interval   = kIntervalDef;
    // The board's name, and the only place it comes from. This used to be
    // a seed that the plugin's own "name" key could then override, which
    // meant two CONFIG pages each offering a field called "Board" and no
    // way for a sysop to tell which one the directory would publish.
    snprintf(g_bbsName, sizeof(g_bbsName), "%.*s", kNameMax, syscfg::get().boardName);
    readServers("http://unleashedbbs.net/announce");         // the default, replaceable
    plugins::forEachKey(g_index, readKey, nullptr);

    for (uint8_t i = 0; i < g_count; ++i) resolve(g_servers[i]);   // quiet board, safe to block
    g_at       = 0;                                                // first heartbeat right away
    g_lastRound = plat::millis();
    g_nextRun  = plat::millis() + static_cast<uint32_t>(g_interval) * 60000u;
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

void stop() {
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    g_stage = Stage::Idle;
    g_at    = 0xFF;
}

// ---------------------------------------------------------------------------
// What CONFIG offers. Declaring these is what makes them reachable: before,
// a plugin's page was built from whatever keys system.cfg already carried,
// so a board that had never written "description" had no way to set one
// short of editing the file by hand.
//
// Labels are nine characters, the width of the form's left column.
// ---------------------------------------------------------------------------
const PluginSetting kSettings[] = {
    // Shown, not editable. The board's name lives in board_name on the core
    // Board page, which every board has whether or not it announces, and
    // this is the value that gets published. Showing it here answers "what
    // will the directory call me" without creating a second copy that can
    // disagree with the first.
    { "name",           "Board",     PS_INFO,  0, 0,     kNameMax },
    { "owner",          "Sysop",     PS_TEXT,  0, 0,     kNameMax },
    { "description",    "About",     PS_TEXT,  0, 0,     kDescMax },
    // Empty means "advertise whatever address the directory saw". A board on
    // a name of its own puts it here: quantum.dnsfor.me, say.
    { "host",           "DNS name",  PS_TEXT,  0, 0,     kUrlMax - 1 },
    { "public_port",    "Port",      PS_NUM,   1, 65535, 5 },
    // Comma separated, so one board can be listed in several directories.
    { "servers",        "Directory", PS_TEXT,  0, 0,     90 },
    { "interval",       "Every min", PS_NUM,   1, 1440,  4 },
    // A caller arriving makes the directory's count wrong at once, so the
    // board pushes an update. This is the shortest gap between pushes;
    // 0 turns them off and leaves only the timed heartbeat.
    { "nudge_seconds",  "Push secs", PS_NUM,   0, 3600,  4 },
    { "share_activity", "Activity",  PS_YESNO, 0, 0,     4 },
    // Issued by the directory and kept so a listing survives a reflash.
    { "token",          "Token",     PS_TEXT,  0, 0,     40 },
};

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
    else if (!strcmp(key, "public_port")) snprintf(out, n, "%u", static_cast<unsigned>(g_public));
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
