/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/sysconfig.cpp
 * Module:       Core / run-time configuration
 *
 * Purpose:      system.cfg loader, validator, password redaction and the
 *                  staff access matrix (see sysconfig.h).
 *
 * Libraries:    none (libc stdio, stdlib, time)
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

#include "sysconfig.h"
#include "users.h"          // LAND_* and the landing names, kept in one place
#include "silent.h"         // silent hours: the time rule, and told when the file is read
#include "../platform/platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#ifdef ESP_PLATFORM
#include "driver/gpio.h"    // GPIO_IS_VALID_GPIO, GPIO_NUM_MAX: the target chip's own pin list
#include "sdkconfig.h"
#endif

const PermName kPermNames[] = {
    { "NODES",     PERM_NODES },
    { "KICK",      PERM_KICK },
    { "BROADCAST", PERM_BROADCAST },
    { "SNOOP",     PERM_SNOOP },
    { "TIME",      PERM_TIME },
    { "BANS",      PERM_BANS },
    { "UNBAN",     PERM_UNBAN },
    { "HIDE",      PERM_HIDE },
    { "NOLIMITS",  PERM_NOLIMITS },
    { "DASH",      PERM_DASH },
    { "USERS",     PERM_USERS },
};
const uint8_t kPermCount = sizeof(kPermNames) / sizeof(kPermNames[0]);

namespace {

SysConfig g_cfg;   // static storage, the live configuration

// Redacted as *** in a backup download and restored from the live value on
// upload. The count is taken from the table, never written beside it.
//
// wifi_password is deliberately NOT here (Rob, NEXT.md 3.2): a backup that
// restores onto a fresh board has to carry the network with it. The backup
// port refuses anything that is not a local address instead, and says so
// when it opens.
const char* const kPasswordKeys[] = { "sysop_password", "cosysop1_password", "cosysop2_password" };
constexpr int kPasswordCount = sizeof(kPasswordKeys) / sizeof(kPasswordKeys[0]);

// verbatim: a key whose value is the rest of the line, '#' and all. A
// passphrase may contain a '#', and cutting it there would store a
// different password from the one that was typed, with nothing said.
bool verbatim(const char* key) {
    if (!strcmp(key, "wifi_ssid") || !strcmp(key, "wifi_password")) return true;
    for (int i = 0; i < kPasswordCount; ++i)
        if (!strcmp(key, kPasswordKeys[i])) return true;
    return false;
}

// stripComment: cut a '#' comment off a line, unless the line sets a
// verbatim key. A '#' at the start of the line is always a comment.
void stripComment(char* line) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '#') {
        const char* eq = strchr(p, '=');
        if (eq) {
            char key[24];
            size_t n = static_cast<size_t>(eq - p);
            while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) --n;
            if (n < sizeof(key)) {
                memcpy(key, p, n);
                key[n] = '\0';
                if (verbatim(key)) return;
            }
        }
    }
    char* hash = strchr(line, '#');
    if (hash) *hash = '\0';
}

// ---------------------------------------------------------------------------
// Parse context: where problems are counted and the first one kept
// ---------------------------------------------------------------------------
struct Ctx {
    SysConfig* cfg;
    int        problems;
    char*      err;
    size_t     errLen;
    int        lineNo;
    // A trial for a writer (syscfg::trial) rather than a file being read:
    // the message is the rule alone, short enough for a form's status line,
    // with no line number and no echo of the value the writer just typed.
    bool       bare = false;
};

void problem(Ctx& c, const char* what, const char* detail) {
    if (!c.problems && c.err && c.errLen) {
        if (c.bare) {
            snprintf(c.err, c.errLen, "%s", what);
            size_t n = strlen(c.err);
            while (n && (c.err[n - 1] == ':' || c.err[n - 1] == ' ')) c.err[--n] = '\0';
        } else {
            snprintf(c.err, c.errLen, "system.cfg line %d: %s %s", c.lineNo, what, detail);
        }
    }
    ++c.problems;
}

// trim: strip leading/trailing whitespace in place, return the start
char* trim(char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) --n;
    s[n] = '\0';
    return s;
}

void copyStr(char* dst, size_t cap, const char* src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

bool ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) != toupper(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// The numeric keys and what the parser takes for each. One table, read by
// the parser below, which CONFIG reaches through syscfg::trial() before it
// writes anything, because CONFIG kept its own copy of these ranges and it drifted three
// ways: it offered 1..120 minutes for a window the parser caps at 60,
// refused the 0 that means "never" for the idle and call limits, and could
// not say -1, "no pin", for either GPIO at all.
struct NumKey { const char* key; long lo; long hi; };
const NumKey kNumKeys[] = {
    { "idle_minutes",          0,  1440 },          // 0 = never
    { "call_minutes",          0,  1440 },          // 0 = unlimited
    { "day_minutes",           0,  1440 },          // 0 = unlimited
    { "guest_minutes",         0,  1440 },          // 0 = unlimited (Bbs::secondsLeft)
    { "backup_window_minutes", 1,  60 },
    { "backup_button_gpio",   -1,  39 },            // -1 = no button
    { "activity_led_gpio",    -1,  39 },            // -1 = no LED
    { "max_users",             1,  BBS_MAX_USERS },
    { "who_refresh_min",       1,  60 },
    { "who_refresh_max",       1,  60 },
    { "port",                  1,  65535 },
    { "backup_port",           1,  65535 },
};

const NumKey* numKey(const char* key) {
    for (const NumKey& k : kNumKeys)
        if (!strcmp(k.key, key)) return &k;
    return nullptr;
}

// number: whole decimal in the key's range, else a problem and false
bool number(Ctx& c, const char* key, const char* v, long& out) {
    const NumKey* k = numKey(key);
    if (!k) { problem(c, key, "has no range"); return false; }   // a key missing from the table
    char* end = nullptr;
    long n = strtol(v, &end, 10);
    if (!*v || (end && *end) || n < k->lo || n > k->hi) {
        char d[48];
        if (c.bare) {
            snprintf(d, sizeof(d), "Between %ld and %ld", k->lo, k->hi);
            problem(c, d, "");
        } else {
            snprintf(d, sizeof(d), "(%ld..%ld)", k->lo, k->hi);
            problem(c, key, d);
        }
        return false;
    }
    out = n;
    return true;
}

// cell: one matrix cell. 1 allowed, 0 denied, -1 unreadable.
int cell(const char* tok) {
    if (!tok) return -1;
    if (ieq(tok, "X") || ieq(tok, "Y") || ieq(tok, "YES")) return 1;
    if (!strcmp(tok, "-") || !strcmp(tok, ".") || ieq(tok, "N") || ieq(tok, "NO")) return 0;
    return -1;
}

// accessRow: "NAME SYSOP CO1 CO2"
void accessRow(Ctx& c, char* line) {
    char* tok[4] = {};
    uint8_t n = 0;
    char* save = nullptr;
    for (char* p = strtok_r(line, " \t", &save); p && n < 4; p = strtok_r(nullptr, " \t", &save)) tok[n++] = p;
    if (n == 0) return;
    if (n < 4) { problem(c, "[access] row needs SYSOP CO1 CO2 columns:", tok[0]); return; }

    uint16_t bit = 0;
    for (uint8_t i = 0; i < kPermCount; ++i) {
        if (ieq(tok[0], kPermNames[i].name)) { bit = kPermNames[i].bit; break; }
    }
    if (!bit) { problem(c, "[access] unknown permission", tok[0]); return; }

    for (uint8_t col = 0; col < 2; ++col) {
        int v = cell(tok[2 + col]);
        if (v < 0) { problem(c, "[access] use X or - in", tok[0]); continue; }
        uint16_t& perms = c.cfg->coPerms[col];
        perms = static_cast<uint16_t>(v ? (perms | bit) : (perms & ~bit));
    }
}

// pinExists: does the chip this firmware was built for have a GPIO with this
// number? A pin it does not have is not an error anywhere: the GPIO driver
// refuses it, the plugin says it "would not start", and the sysop is left to
// work out that CONFIG took a number the chip never had (the WROOM has no 20,
// 24 or 28 to 31). On the board the IDF's soc caps answer, so the rule for
// another chip arrives with its build target and nothing here changes. The
// host has no chip, and stands in for the reference board, the WROOM, or for
// the S3 when it is built for an S3 board profile (board.h, BBS_CHIP_S3):
// 0 to 48, less 22 to 25, which the S3 does not have.
bool pinExists(long pin) {
#ifdef ESP_PLATFORM
    if (pin < 0 || pin >= GPIO_NUM_MAX) return false;
    if (!GPIO_IS_VALID_GPIO(static_cast<int>(pin))) return false;
#if CONFIG_IDF_TARGET_ESP32
    // In the ESP32's list, but bonded out only on the PICO-V3 packages. The
    // chip on a WROOM has no pad for it, and the WROOM is the floor.
    if (pin == 20) return false;
#endif
    return true;
#elif defined(BBS_CHIP_S3)
    return pin >= 0 && pin <= 48 && !(pin >= 22 && pin <= 25);
#else
    return pin >= 0 && pin <= 39 && pin != 20 && pin != 24 && !(pin >= 28 && pin <= 31);
#endif
}

// The board's own pins (board.h, BBS_PINS_*): pins that exist and are not the
// flash, but that this board has already wired to something, a camera, a
// card slot, its PSRAM. Only a profile that names them has any, so the
// reference board's rule is the chip's alone.
#if defined(BBS_PINS_PSRAM) || defined(BBS_PINS_CONSOLE) || defined(BBS_PINS_CARD) || \
    defined(BBS_PINS_CAMERA) || defined(BBS_PINS_STRAP)
#define BBS_HAS_BOARD_PINS 1
struct BoardPins { const int8_t* pins; uint8_t count; const char* problem; const char* sentence; };
#ifdef BBS_PINS_PSRAM
constexpr int8_t kPinsPsram[]   = { BBS_PINS_PSRAM };
#endif
#ifdef BBS_PINS_CONSOLE
constexpr int8_t kPinsConsole[] = { BBS_PINS_CONSOLE };
#endif
#ifdef BBS_PINS_CARD
constexpr int8_t kPinsCard[]    = { BBS_PINS_CARD };
#endif
#ifdef BBS_PINS_CAMERA
constexpr int8_t kPinsCamera[]  = { BBS_PINS_CAMERA };
#endif
#ifdef BBS_PINS_STRAP
constexpr int8_t kPinsStrap[]   = { BBS_PINS_STRAP };
#endif
#define BBS_PINROW(a, p, s) { a, static_cast<uint8_t>(sizeof(a)), p, s }
// Both columns fit a form's status line: a core key shows the problem
// (cut at 39 by trial), a plugin's page the sentence (38 or fewer).
constexpr BoardPins kBoardPins[] = {
#ifdef BBS_PINS_PSRAM
    BBS_PINROW(kPinsPsram,   "that pin is the board's PSRAM",       "That pin is the board's PSRAM."),
#endif
#ifdef BBS_PINS_CONSOLE
    BBS_PINROW(kPinsConsole, "that pin is the console and Improv",  "That pin is the console and Improv."),
#endif
#ifdef BBS_PINS_CARD
    BBS_PINROW(kPinsCard,    "that pin is the SD card slot",        "That pin is the SD card slot."),
#endif
#ifdef BBS_PINS_CAMERA
    BBS_PINROW(kPinsCamera,  "that pin is the camera's",            "That pin is the camera's."),
#endif
#ifdef BBS_PINS_STRAP
    BBS_PINROW(kPinsStrap,   "a strapping pin, low at boot",        "That is a strapping pin."),
#endif
};
#undef BBS_PINROW

const BoardPins* boardPin(long pin) {
    for (const BoardPins& b : kBoardPins)
        for (uint8_t i = 0; i < b.count; ++i)
            if (b.pins[i] == pin) return &b;
    return nullptr;
}
#endif

// gpio: a pin number in the key's range that the board may use (see
// syscfg::pinProblem); -1, "none", is in every pin key's range.
bool gpio(Ctx& c, const char* key, const char* v, long& out) {
    if (!number(c, key, v, out)) return false;
#ifdef BBS_HAS_BOARD_PINS
    // A file that names a pin this board has already wired to something (a
    // WROOM's system.cfg restored here, whose LED is on 2, this board's card)
    // loses that one line, logged, rather than the whole file: refusing it
    // would throw away the network, the access matrix and every plugin
    // section over a pin whose safe meaning is "leave it alone" (the 1.0.2
    // rule: dropped, not refused). A writer (CONFIG) is still refused, below.
    if (!c.bare) {
        if (const BoardPins* b = boardPin(out)) {
            plat::log("cfg: line %d %s = %ld: %s on this board, line ignored",
                      c.lineNo, key, out, b->problem);
            return false;
        }
    }
#endif
    if (const char* why = syscfg::pinProblem(out)) {
        char what[48];
        snprintf(what, sizeof(what), "%s:", why);
        problem(c, what, v);
        return false;
    }
    return true;
}

// yesNo: yes/no/on/off/1/0
void yesNo(Ctx& c, const char* key, const char* val, bool& out) {
    if (ieq(val, "yes") || ieq(val, "on") || !strcmp(val, "1"))      out = true;
    else if (ieq(val, "no") || ieq(val, "off") || !strcmp(val, "0")) out = false;
    else {
        char what[48];
        snprintf(what, sizeof(what), "%.24s must be yes or no:", key);
        problem(c, what, val);
    }
}

// keyValue: one "key = value" line
void keyValue(Ctx& c, const char* key, char* val) {
    SysConfig& g = *c.cfg;
    long n = 0;
    if (!strcmp(key, "hostname")) {
        if (syscfg::validHostname(val)) copyStr(g.hostname, sizeof(g.hostname), val);
        else problem(c, "hostname must be a-z 0-9 - (1..31):", val);
    }
    else if (!strcmp(key, "board_name"))             copyStr(g.boardName, sizeof(g.boardName), val);
    else if (!strcmp(key, "tz"))                     copyStr(g.tz, sizeof(g.tz), val);
    else if (!strcmp(key, "ntp_server"))             copyStr(g.ntpServer, sizeof(g.ntpServer), val);
    else if (!strcmp(key, "sysop_password"))         copyStr(g.sysopPass, sizeof(g.sysopPass), val);
    else if (!strcmp(key, "cosysop1_password"))      copyStr(g.coPass[0], sizeof(g.coPass[0]), val);
    else if (!strcmp(key, "cosysop2_password"))      copyStr(g.coPass[1], sizeof(g.coPass[1]), val);
    // The sysop's account (1.1.0). Whether the account exists is CONFIG's
    // question, asked when it is saved: this parser knows nothing of
    // users.txt, and a restore checks the two files apart.
    else if (!strcmp(key, "sysop_handle")) {
        if (*val && !users::validHandle(val)) problem(c, "sysop_handle is not a handle:", val);
        else copyStr(g.sysopHandle, sizeof(g.sysopHandle), val);
    }
    else if (!strcmp(key, "sysop_id")) {
        // An account id: digits, 0 for none. Not number(), whose ranges are
        // a long's, and an id is a uint32_t.
        // Empty is 0, not set: a hand edit that leaves "sysop_id =" must
        // not refuse the whole file.
        if (!*val) { g.sysopId = 0; return; }
        bool digits = strlen(val) <= 10;
        for (const char* p = val; *p; ++p) if (*p < '0' || *p > '9') digits = false;
        unsigned long long id = digits ? strtoull(val, nullptr, 10) : 0;
        if (!digits || id > 0xFFFFFFFFull) problem(c, "sysop_id must be an account id:", val);
        else g.sysopId = static_cast<uint32_t>(id);
    }
    else if (!strcmp(key, "wifi_ssid")) {
        if (strlen(val) >= sizeof(g.wifiSsid)) problem(c, "wifi_ssid is longer than 32", "");
        else copyStr(g.wifiSsid, sizeof(g.wifiSsid), val);
    }
    else if (!strcmp(key, "wifi_password")) {
        // WPA2 wants 8 to 63 characters, or 64 hex digits for a raw key;
        // empty is an open network. Anything else would be refused by the
        // radio with a reason code nobody would trace back to this line.
        size_t n = strlen(val);
        if (n && (n < 8 || n >= sizeof(g.wifiPass))) problem(c, "wifi_password must be 8 to 64 characters", "");
        else copyStr(g.wifiPass, sizeof(g.wifiPass), val);
    }
    else if (!strcmp(key, "idle_minutes"))          { if (number(c, key, val, n)) g.idleMinutes = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "landing")) {
        // "default" is not a board default, so it is refused rather than
        // quietly meaning main: a board whose default is "whatever the
        // default is" is a setting that says nothing.
        uint8_t v = users::landFromKey(val);
        if (v == LAND_DEFAULT) problem(c, "landing must be main, chat or forums", val);
        else g.landing = v;
    }
    else if (!strcmp(key, "call_minutes"))          { if (number(c, key, val, n)) g.callMinutes = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "day_minutes"))           { if (number(c, key, val, n)) g.dayMinutes  = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "backup_window_minutes")) { if (number(c, key, val, n)) g.backupMinutes = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "backup_button_gpio"))    { if (gpio(c, key, val, n)) g.backupGpio = static_cast<int8_t>(n); }
    else if (!strcmp(key, "activity_led_gpio"))     { if (gpio(c, key, val, n)) g.ledGpio = static_cast<int8_t>(n); }
    else if (!strcmp(key, "max_users"))             { if (number(c, key, val, n)) g.maxUsers = static_cast<uint8_t>(n); }
    else if (!strcmp(key, "self_register"))         yesNo(c, key, val, g.selfRegister);
    else if (!strcmp(key, "guest"))                 yesNo(c, key, val, g.guestEnabled);
    else if (!strcmp(key, "guest_minutes"))         { if (number(c, key, val, n)) g.guestMinutes = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "who_refresh_min"))       { if (number(c, key, val, n)) g.whoMin = static_cast<uint8_t>(n); }
    else if (!strcmp(key, "who_refresh_max"))       { if (number(c, key, val, n)) g.whoMax = static_cast<uint8_t>(n); }
    // The two ports are only checked against each other in crossCheck: a
    // file may set them in either order. backup_port used to be checked
    // here against the constant 6400, which stopped being the rule the day
    // the listening port became a setting.
    else if (!strcmp(key, "port"))                  { if (number(c, key, val, n)) g.port = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "backup_port"))           { if (number(c, key, val, n)) g.backupPort = static_cast<uint16_t>(n); }
    // Silent mode (1.1.0, core/silent). The hours are a time of day or blank;
    // whether both ends are set is crossCheck's, once every line is in.
    else if (!strcmp(key, "silent"))                yesNo(c, key, val, g.silent);
    else if (!strcmp(key, "closed")) {
        // Set only when the value was read: a bad line is a problem, and a
        // board on the default must then stay closed, not fall open.
        int before = c.problems;
        yesNo(c, key, val, g.closed);
        if (c.problems == before) g.closedSet = true;
    }
    else if (!strcmp(key, "silent_from") || !strcmp(key, "silent_until")) {
        // A form is told (a writer's trial); a file is read as no time and
        // the console says so, for the reason crossCheck gives below.
        int16_t t = board::parseTime(val);
        if (t == board::kBadTime && c.bare) {
            problem(c, "HH:MM, 00:00 to 23:59, or blank", val);
        } else if (t == board::kBadTime) {
            plat::log("cfg: line %d %s = %s is not HH:MM, read as none", c.lineNo, key, val);
            if (key[7] == 'f') g.silentFrom = board::kNoTime;
            else               g.silentUntil = board::kNoTime;
        } else if (key[7] == 'f') {
            g.silentFrom = t;
        } else {
            g.silentUntil = t;
        }
    }
    else plat::log("cfg: line %d unknown key '%s' ignored", c.lineNo, key);
}

// crossCheck: the rules about two keys at once, which can only be judged
// once every line is in. Returns the key it objects to, or nullptr.
// portWritten: a writer's trial that sets port, so a clash is said about
// the port rather than about the backup window.
const char* crossCheck(Ctx& c, SysConfig& out, bool portWritten = false) {
    if (out.whoMin > out.whoMax) {
        c.lineNo = 0;
        problem(c, "who_refresh_min is above the max", "");
        out.whoMin = out.whoMax;
        return "who_refresh_min";
    }
    // One port, two listeners: the backup window would fail to open, or
    // worse, open over the callers' line. Said in the words of the page the
    // sysop is on; a file gets the sentence the backup page always had.
    if (out.port == out.backupPort) {
        c.lineNo = 0;
        char num[8];
        snprintf(num, sizeof(num), "%u", static_cast<unsigned>(out.port));
        if (portWritten) problem(c, "Same as the backup port. Pick another.", "");
        else             problem(c, "backup_port cannot be the BBS port:", num);
        // A file read at boot still has to come up somewhere. Keep the port
        // callers dial and put the window back on its default, or, when the
        // default is the port being dialled, the listener back on its own.
        if (out.backupPort != BBS_BACKUP_PORT) out.backupPort = BBS_BACKUP_PORT;
        else                                   out.port       = BBS_PORT;
        return portWritten ? "port" : "backup_port";
    }
    // Silent hours are two ends or none. From a form (a writer's trial) half
    // a range is refused where it can be finished; from a file it is read as
    // none and said on the console, because refusing the whole file over the
    // one setting that only turns lights off would take every other line on
    // the page down with it.
    const bool fromSet = out.silentFrom >= 0, untilSet = out.silentUntil >= 0;
    if (fromSet != untilSet || (fromSet && out.silentFrom == out.silentUntil)) {
        const bool same = fromSet && untilSet;
        const char* key = same ? "silent_until" : (fromSet ? "silent_until" : "silent_from");
        if (c.bare) {
            problem(c, same ? "The same time twice: no silent hours" : "Set both times, or neither", "");
        } else {
            plat::log("cfg: silent hours %s, read as none",
                      same ? "start and end at the same time" : "have only one end");
        }
        out.silentFrom = out.silentUntil = board::kNoTime;
        return c.bare ? key : nullptr;
    }
    return nullptr;
}

// A scratch configuration, for a reload that may yet be refused, for a
// writer's trial, and for check()ing a restore's uploaded file. Static
// because SysConfig is ~410 bytes and the BBS task's stack is not the place
// for it; one is enough, since all three run to completion on the BBS task
// and none of them calls another.
SysConfig g_scratch;

// ---------------------------------------------------------------------------
// ctEqual: constant-time compare over the whole password field
// ---------------------------------------------------------------------------
bool ctEqual(const char* pw, size_t cap, const char* candidate) {
    size_t a = strlen(pw);
    size_t b = strlen(candidate);
    uint8_t diff = static_cast<uint8_t>(a != b);
    for (size_t i = 0; i < cap; ++i) {
        uint8_t x = i < a ? static_cast<uint8_t>(pw[i]) : 0;
        uint8_t y = i < b ? static_cast<uint8_t>(candidate[i]) : 0;
        diff |= static_cast<uint8_t>(x ^ y);
    }
    return diff == 0 && a > 0;
}

// ---------------------------------------------------------------------------
// passwordAssignment: is this line "<password key> = value"? Fills the key
// index and a pointer to the trimmed value (inside tmp).
// ---------------------------------------------------------------------------
bool passwordAssignment(const char* line, char* tmp, size_t tmpLen, int& keyIdx, char*& value) {
    copyStr(tmp, tmpLen, line);
    stripComment(tmp);
    char* eq = strchr(tmp, '=');
    if (!eq) return false;
    *eq = '\0';
    char* key = trim(tmp);
    for (int i = 0; i < kPasswordCount; ++i) {
        if (!strcmp(key, kPasswordKeys[i])) {
            keyIdx = i;
            value  = trim(eq + 1);
            return true;
        }
    }
    return false;
}

const char* livePassword(int idx) {
    switch (idx) {
        case 0:  return g_cfg.sysopPass;
        case 1:  return g_cfg.coPass[0];
        default: return g_cfg.coPass[1];
    }
}

void logSummary() {
    plat::log("cfg: %s  host %s  tz %s  ntp %s",
              g_cfg.fromFile ? BBS_CONFIG_FILE : "defaults (no " BBS_CONFIG_FILE ")",
              g_cfg.hostname, g_cfg.tz, g_cfg.ntpServer);
    plat::log("cfg: port %u  idle %u  limits %u/call %u/day  backup port %u, %u min, gpio %d",
              g_cfg.port, g_cfg.idleMinutes, g_cfg.callMinutes, g_cfg.dayMinutes,
              g_cfg.backupPort, g_cfg.backupMinutes, g_cfg.backupGpio);
    plat::log("cfg: who refresh %u..%u s  activity led gpio %d  self_register %s  max_users %u  guest %s %u min",
              g_cfg.whoMin, g_cfg.whoMax, g_cfg.ledGpio, g_cfg.selfRegister ? "yes" : "no", g_cfg.maxUsers,
              g_cfg.guestEnabled ? "yes" : "no", g_cfg.guestMinutes);
    plat::log("cfg: wifi %s", g_cfg.wifiSsid[0] ? g_cfg.wifiSsid : "not set in the file");
    // Before 0.22.1 a '#' anywhere began a comment, so a hand-edited
    // "sysop_password = x   # note" meant x. It now means the whole line,
    // and BYE x becomes a plain logoff that counts toward a ban with
    // nothing saying why. Say it here, by key and never by value.
    const char* const keys[] = { "sysop_password", "cosysop1_password", "cosysop2_password",
                                 "wifi_ssid", "wifi_password" };
    const char* const vals[] = { g_cfg.sysopPass, g_cfg.coPass[0], g_cfg.coPass[1],
                                 g_cfg.wifiSsid, g_cfg.wifiPass };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (strstr(vals[i], " #") || strstr(vals[i], "\t#"))
            plat::log("cfg: %s has a ' #' in it; since 0.22.1 that is part of the value, not a comment",
                      keys[i]);
    // A sysop_password line that spells out the published default reads as
    // the default (parseFile), so it gets the default's line here too.
    if (g_cfg.closed)
        plat::log("cfg: CLOSED to callers%s; CONFIG board opens it",
                  g_cfg.closedSet ? "" : " (a board on the default starts closed)");
    plat::log("cfg: sysop %s  co1 %s perms 0x%03x  co2 %s perms 0x%03x",     // never the passwords
              g_cfg.sysopDefault ? "on the published default, local network only"
                                 : g_cfg.sysopPass[0] ? "on" : "off",
              g_cfg.coPass[0][0] ? "on" : "off", g_cfg.coPerms[0],
              g_cfg.coPass[1][0] ? "on" : "off", g_cfg.coPerms[1]);
}

} // namespace

namespace syscfg {

// ---------------------------------------------------------------------------
// parseFile: key=value lines and the [access] section
// ---------------------------------------------------------------------------
// useDefaultSysop: no sysop_password line anywhere, or one that names the
// published default, so the published default stands in, with the limits
// that come with it (see BBS_DEFAULT_SYSOP).
static void useDefaultSysop(SysConfig& out) {
    copyStr(out.sysopPass, sizeof(out.sysopPass), BBS_DEFAULT_SYSOP);
    out.sysopDefault = true;
}

// closedDefault: no closed line, so the board is closed exactly while it is
// still on the published default (1.1.0). A fresh board starts closed; a
// board whose sysop already chose a password, which is every board set up
// before 1.1.0, stays open across the upgrade.
static void closedDefault(SysConfig& out) {
    if (!out.closedSet) out.closed = out.sysopDefault;
}

int parseFile(const char* path, SysConfig& out, char* err, size_t errLen) {
    Ctx c{ &out, 0, err, errLen, 0 };
    if (err && errLen) err[0] = '\0';
    FILE* f = fopen(path, "r");
    if (!f) {                                       // no file: defaults, not a problem
        useDefaultSysop(out);
        closedDefault(out);
        return 0;
    }

    char line[160];
    bool inAccess = false;
    bool inPlugin = false;
    bool sawSysop = false;                          // any sysop_password line at all
    while (fgets(line, sizeof(line), f)) {
        ++c.lineNo;
        if (!strchr(line, '\n') && !feof(f)) {      // overlong line: skip the rest of it
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {}
            problem(c, "line too long", "");
            continue;
        }
        stripComment(line);
        char* l = trim(line);
        if (!*l) continue;

        if (*l == '[') {
            inAccess = ieq(l, "[access]");
            inPlugin = !inAccess && !strncmp(l, "[plugin:", 8);   // read by plugin.cpp
            if (!inAccess && !inPlugin) problem(c, "unknown section", l);
            continue;
        }
        if (inPlugin) continue;                     // a plugin's own keys
        if (inAccess) { accessRow(c, l); continue; }

        char* eq = strchr(l, '=');
        if (!eq) { problem(c, "expected key = value:", l); continue; }
        *eq = '\0';
        char* key = trim(l);
        char* val = trim(eq + 1);
        if (!strcmp(key, "sysop_password")) sawSysop = true;   // even redacted, even empty
        if (!strcmp(val, "***")) continue;          // redacted password: keep what is set
        keyValue(c, key, val);
    }
    fclose(f);
    // A line that spells the published password out is the default too
    // (1.1.0), exactly as if it were absent: local only, the listing held,
    // setup offered. Nothing writes one since 1.0.2, but a restore on 1.0.0
    // or 1.0.1 did, and on those boards the password printed on the install
    // page worked from anywhere and the board went on the directory. Read
    // this way, such a board heals at its next boot with nothing rewritten.
    if (!sawSysop || !strcmp(out.sysopPass, BBS_DEFAULT_SYSOP)) useDefaultSysop(out);
    closedDefault(out);
    crossCheck(c, out);
    out.fromFile = true;
    return c.problems;
}

// ---------------------------------------------------------------------------
// seed: the very first boot on a blank user partition. A filesystem upload
// can only write the storage partition, so a config shipped with the screens
// is copied across once and then belongs to the board. Every later edit goes
// to the user partition and survives a reflash.
// ---------------------------------------------------------------------------
static void seed() {
    char live[96], shipped[96];
    snprintf(live, sizeof(live), "%s/%s", plat::userBase(), BBS_CONFIG_FILE);
    snprintf(shipped, sizeof(shipped), "%s/%s", plat::fsBase(), BBS_CONFIG_FILE);

    FILE* have = fopen(live, "r");
    if (have) { fclose(have); return; }              // already ours, leave it
    FILE* from = fopen(shipped, "r");
    if (!from) return;                               // nothing to seed from
    FILE* to = fopen(live, "w");
    if (!to) { fclose(from); return; }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), from)) > 0) fwrite(buf, 1, n, to);
    fclose(from);
    fclose(to);
    plat::log("cfg: seeded %s from the shipped copy", live);
}

bool load() {
    char path[96];
    seed();
    snprintf(path, sizeof(path), "%s/%s", plat::userBase(), BBS_CONFIG_FILE);
    char err[96];
    int problems = parseFile(path, g_cfg, err, sizeof(err));
    if (problems) plat::log("cfg: %d problem(s), first: %s", problems, err);
    setenv("TZ", g_cfg.tz, 1);
    tzset();
    logSummary();
    board::silentTick(plat::millis(), true);     // the switch holds from the first light
    return g_cfg.fromFile;
}

bool reload(char* err, size_t errLen) {
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", plat::userBase(), BBS_CONFIG_FILE);
    g_scratch = SysConfig();
    if (parseFile(path, g_scratch, err, errLen)) return false;
    g_cfg = g_scratch;
    setenv("TZ", g_cfg.tz, 1);
    tzset();
    logSummary();
    // Before anything restarts on the new settings: a plugin that lights
    // something asks silent() as it starts, and must get this file's answer.
    board::silentTick(plat::millis(), true);
    return true;
}

int check(const char* path, char* err, size_t errLen, uint8_t* maxUsers) {
    g_scratch = SysConfig();
    int problems = parseFile(path, g_scratch, err, errLen);
    if (maxUsers) *maxUsers = g_scratch.maxUsers;
    // Forgotten at once: the file came from outside and carries passwords,
    // and nothing is ever run on it. The next reload or trial starts over
    // anyway, so this costs one struct assignment and hides nothing.
    g_scratch = SysConfig();
    return problems;
}

const SysConfig& get() {
    return g_cfg;
}

Access passwordLevel(const char* candidate) {
    if (!candidate || !*candidate || !strcmp(candidate, "***")) return Access::None;
    bool sys = ctEqual(g_cfg.sysopPass, sizeof(g_cfg.sysopPass), candidate);
    bool co1 = ctEqual(g_cfg.coPass[0], sizeof(g_cfg.coPass[0]), candidate);
    bool co2 = ctEqual(g_cfg.coPass[1], sizeof(g_cfg.coPass[1]), candidate);
    if (sys) return Access::Sysop;
    if (co1) return Access::CoSysop1;
    if (co2) return Access::CoSysop2;
    return Access::None;
}

uint16_t permsFor(Access level) {
    switch (level) {
        case Access::Sysop:    return PERM_ALL;
        case Access::CoSysop1: return g_cfg.coPerms[0];
        case Access::CoSysop2: return g_cfg.coPerms[1];
        default:               return 0;
    }
}

bool anyPassword() {
    return g_cfg.sysopPass[0] || g_cfg.coPass[0][0] || g_cfg.coPass[1][0];
}

const char* levelName(Access level) {
    switch (level) {
        case Access::Sysop:    return "Sysop";
        case Access::CoSysop1: return "Co-sysop 1";
        case Access::CoSysop2: return "Co-sysop 2";
        default:               return "";
    }
}

// ---------------------------------------------------------------------------
// The parser's rules, for writers (see sysconfig.h)
// ---------------------------------------------------------------------------

bool validHostname(char* v) {
    size_t n = strlen(v);
    if (!n || n >= 32 || v[0] == '-' || v[n - 1] == '-') return false;
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<char>(tolower(static_cast<unsigned char>(v[i])));
        if (!isalnum(static_cast<unsigned char>(v[i])) && v[i] != '-') return false;
    }
    return true;
}

void normaliseHostname(char* v) {
    char* s = trim(v);
    if (s != v) memmove(v, s, strlen(s) + 1);
    size_t n = strlen(v);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<char>(tolower(static_cast<unsigned char>(v[i])));
    while (n && v[n - 1] == '.') v[--n] = '\0';
    // One ".local", not every one: "a.local.local" is somebody's typo and
    // the check that follows should see what is left of it.
    const size_t kLocal = 6;                         // ".local"
    if (n > kLocal && !strcmp(v + n - kLocal, ".local")) { n -= kLocal; v[n] = '\0'; }
    while (n && v[n - 1] == '.') v[--n] = '\0';
}

// The rule is the chip's (1.1.0). On the ESP32, the WROOM's rule, unchanged:
// 6 to 11 are the flash. On the S3 those are ordinary pins, and the ones
// that must never be handed out are elsewhere:
//   26 to 32  the flash and the PSRAM's shared bus (SPICS1, SPIHD, SPIWP,
//             SPICS0, SPICLK, SPIQ, SPID; ESP32-S3 datasheet table 2-14)
//   33 to 37  octal PSRAM's DQ4 to DQ7 and DQS, on an R8 like the Waveshare
//             stick's. Refused on every S3 build: the one S3 board this
//             firmware knows has octal PSRAM, and a quad part that frees
//             them is a board profile's business when one arrives
//   19, 20    the chip's own USB. On a board with no USB-serial bridge it is
//             the only way in, and a pin taken from it makes the board
//             vanish from the computer until it is put into download mode
//             by hand (the IDF's USB-Serial-JTAG console guide)
// The flash pins first, on either chip: they are real pins, and why not to
// use them is more use to a sysop than "no such pin". Then the pins the
// board itself owns (kBoardPins, above gpio()).
const char* pinProblem(long pin) {
    if (pin == -1) return nullptr;                   // "none", in every pin key's range
#ifdef BBS_CHIP_S3
    if (pin >= 26 && pin <= 37) return "pins 26-37 are the flash and PSRAM";
    if (pin == 19 || pin == 20) return "pins 19 and 20 are the USB port";
#else
    if (pin >= 6 && pin <= 11) return "pins 6-11 are the flash chip";
#endif
    if (!pinExists(pin)) return "this chip has no such pin";
#ifdef BBS_HAS_BOARD_PINS
    if (const BoardPins* b = boardPin(pin)) return b->problem;
#endif
    return nullptr;
}

const char* pinSentence(long pin) {
    const char* why = pinProblem(pin);
    if (!why) return nullptr;
#ifdef BBS_HAS_BOARD_PINS
    if (pinExists(pin)) {
        if (const BoardPins* b = boardPin(pin)) return b->sentence;
    }
#endif
#ifdef BBS_CHIP_S3
    if (pin >= 26 && pin <= 37) return "Pins 26 to 37 are flash and PSRAM.";
    if (pin == 19 || pin == 20) return "Pins 19 and 20 are the USB port.";
    return "This chip has no such pin.";
#else
    return pin >= 6 && pin <= 11 ? "Pins 6 to 11 are the flash chip."   // the copy's LT-flash-pin
                                 : "This chip has no such pin.";
#endif
}

const char* trial(const KeyVal* pairs, uint8_t count, char* why, size_t n) {
    g_scratch = g_cfg;
    Ctx c{ &g_scratch, 0, why, n, 0 };
    c.bare = true;
    if (why && n) why[0] = '\0';
    char val[128];
    bool portWritten = false;
    for (uint8_t i = 0; i < count; ++i) {
        const char* key = pairs[i].key;
        const char* v   = pairs[i].value;
        if (!strcmp(key, "port")) portWritten = true;
        size_t len = strlen(v);
        if (len >= sizeof(val)) {
            problem(c, "Too long", "");
            return key;
        }
        memcpy(val, v, len + 1);
        char* t = trim(val);
        // What parseFile does to a line before keyValue ever sees it. Each
        // of these was a way to write a value the board then read back as
        // something else, with nothing said.
        if (!strcmp(t, "***")) {                     // read as "keep the redacted one"
            problem(c, "*** stands for a hidden password", "");
            return key;
        }
        if (!verbatim(key) && strchr(t, '#')) {      // read as the start of a comment
            problem(c, "No # here: it starts a comment", "");
            return key;
        }
        if (verbatim(key) && strlen(t) != len) {     // trimmed, so a different password
            problem(c, "No spaces at either end", "");
            return key;
        }
        keyValue(c, key, t);
        if (c.problems) return key;
    }
    return crossCheck(c, g_scratch, portWritten);
}

// ---------------------------------------------------------------------------
// sameKey: does this line set that key? Leading spaces and the spacing
// around the = are all allowed, so a hand-edited file still matches.
// ---------------------------------------------------------------------------
namespace {

bool sameKey(const char* line, const char* key) {
    while (*line == ' ' || *line == '\t') ++line;
    size_t n = strlen(key);
    if (strncasecmp(line, key, n) != 0) return false;
    const char* p = line + n;
    while (*p == ' ' || *p == '\t') ++p;
    return *p == '=';
}

// sectionOf: "[chat]" -> chat. Returns false for anything else.
bool sectionOf(const char* line, char* out, size_t n) {
    while (*line == ' ' || *line == '\t') ++line;
    if (*line != '[') return false;
    ++line;
    size_t w = 0;
    while (*line && *line != ']' && w + 1 < n) out[w++] = *line++;
    out[w] = '\0';
    return *line == ']';
}

} // namespace

bool write(const KeyVal* pairs, uint8_t count, const char* section, char* err, size_t errLen) {
    char path[160], tmp[176];
    snprintf(path, sizeof(path), "%s/system.cfg", plat::userBase());
    snprintf(tmp, sizeof(tmp), "%s/system.tmp", plat::userBase());

    bool done[16] = {};
    if (count > 16) {
        snprintf(err, errLen, "too many settings at once");
        return false;
    }
    // A null value removes the key rather than setting it (1.1.0, the BOOT
    // reset): every line in the section that sets it is left out, and it is
    // never added at the end. Marked done up front for the second half.
    for (uint8_t i = 0; i < count; ++i) if (!pairs[i].value) done[i] = true;

    FILE* out = fopen(tmp, "w");
    if (!out) {
        snprintf(err, errLen, "cannot write the config file");
        return false;
    }

    FILE* in = fopen(path, "r");
    bool inSection = section == nullptr;        // the top of the file is the unnamed section
    bool seen      = inSection;
    if (in) {
        char line[192];
        while (fgets(line, sizeof(line), in)) {
            char name[40];
            if (sectionOf(line, name, sizeof(name))) {
                if (inSection) {                // leaving the section: add what is missing
                    for (uint8_t i = 0; i < count; ++i)
                        if (!done[i]) { fprintf(out, "%s = %s\n", pairs[i].key, pairs[i].value); done[i] = true; }
                }
                inSection = section && !strcasecmp(name, section);
                if (inSection) seen = true;
            } else if (inSection) {
                bool replaced = false;
                for (uint8_t i = 0; i < count; ++i) {
                    if (!sameKey(line, pairs[i].key)) continue;
                    if (!pairs[i].value) { replaced = true; break; }   // removed: every such line
                    if (done[i]) continue;
                    fprintf(out, "%s = %s\n", pairs[i].key, pairs[i].value);
                    done[i] = replaced = true;
                    break;
                }
                if (replaced) continue;
            }
            fputs(line, out);
            size_t len = strlen(line);
            if (len && line[len - 1] != '\n') fputc('\n', out);
        }
        fclose(in);
    }

    bool missing = false;
    for (uint8_t i = 0; i < count; ++i) if (!done[i]) missing = true;
    if (missing) {
        if (!seen && section) fprintf(out, "\n[%s]\n", section);
        for (uint8_t i = 0; i < count; ++i)
            if (!done[i]) fprintf(out, "%s = %s\n", pairs[i].key, pairs[i].value);
    }

    if (fflush(out) != 0 || fclose(out) != 0) {
        remove(tmp);
        snprintf(err, errLen, "the config file could not be finished");
        return false;
    }
    // Renamed over the old file, never removed first (1.1.0). system.cfg
    // lives on userdata, which is LittleFS on the board: esp_littlefs's
    // rename is lfs_rename, which replaces an existing file in one step, and
    // so does POSIX on the host. Removing first opened a hole: a failure
    // between the two left no system.cfg at all, and the next boot came up
    // with no network, no staff passwords and the published default. Only
    // FatFs refuses an existing destination, and this file is never on the
    // card. So a failure here leaves the old file exactly as it was, which
    // is what "Nothing changed." after a failed BOOT reset promises.
    if (rename(tmp, path) != 0) {
        remove(tmp);
        snprintf(err, errLen, "the new config file could not be put in place");
        return false;
    }
    return true;
}

bool redactLine(const char* line, char* out, size_t outLen) {
    char tmp[160];
    int idx = 0;
    char* value = nullptr;
    if (!passwordAssignment(line, tmp, sizeof(tmp), idx, value) || !*value) return false;
    snprintf(out, outLen, "%s = ***\n", kPasswordKeys[idx]);
    return true;
}

// unredactLine: a staff password line from a restored system.cfg, as it is
// written on this board. A download never carries a staff password, only
// ***, so *** means "keep this board's own" and is written as the live one.
//
// The published default is never written, in either form (1.0.2). A board
// on the default has no sysop_password line, and that absence is all that
// keeps the default local-only and the directory listing held. 1.0.1 wrote
// *** back as the live password, which on such a board IS the published
// one, and the explicit line made it a real password that worked from
// anywhere, on a board that then went on the directory. So a line whose
// value would be the published password, restored from *** or typed out,
// comes back empty and is left out: for the sysop that is the default, for
// a co-sysop it is that level off.
bool unredactLine(const char* line, char* out, size_t outLen) {
    char tmp[160];
    int idx = 0;
    char* value = nullptr;
    if (!passwordAssignment(line, tmp, sizeof(tmp), idx, value)) return false;
    const bool redacted = !strcmp(value, "***");
    const char* pw = redacted ? livePassword(idx) : value;
    if (!strcmp(pw, BBS_DEFAULT_SYSOP)) {           // published: leave the line out
        if (outLen) out[0] = '\0';
        return true;
    }
    if (!redacted) return false;                    // a password of its own: as typed
    snprintf(out, outLen, "%s = %s\n", kPasswordKeys[idx], pw);
    return true;
}

} // namespace syscfg
