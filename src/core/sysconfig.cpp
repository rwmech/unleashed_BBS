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

#include "sysconfig.h"
#include "users.h"          // LAND_* and the landing names, kept in one place
#include "../platform/platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>

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

// gpio: a pin number in the key's range that the board may use (see
// syscfg::pinProblem); -1, "none", is in every pin key's range.
bool gpio(Ctx& c, const char* key, const char* v, long& out) {
    if (!number(c, key, v, out)) return false;
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
    else if (!strcmp(key, "backup_port")) {
        if (number(c, key, val, n)) {
            if (n == BBS_PORT) problem(c, "backup_port cannot be the BBS port", val);
            else g.backupPort = static_cast<uint16_t>(n);
        }
    }
    else plat::log("cfg: line %d unknown key '%s' ignored", c.lineNo, key);
}

// crossCheck: the rules about two keys at once, which can only be judged
// once every line is in. Returns the key it objects to, or nullptr.
const char* crossCheck(Ctx& c, SysConfig& out) {
    if (out.whoMin > out.whoMax) {
        c.lineNo = 0;
        problem(c, "who_refresh_min is above the max", "");
        out.whoMin = out.whoMax;
        return "who_refresh_min";
    }
    return nullptr;
}

// A scratch configuration, for a reload that may yet be refused and for a
// writer's trial. Static because SysConfig is ~420 bytes and the BBS task's
// stack is not the place for it; one is enough, since neither can be in
// progress while the other is.
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
    plat::log("cfg: idle %u  limits %u/call %u/day  backup port %u, %u min, gpio %d",
              g_cfg.idleMinutes, g_cfg.callMinutes, g_cfg.dayMinutes,
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
    // A sysop_password line that spells out the published default is a real
    // password to the board and works from anywhere. Nothing writes one since
    // 1.0.2, but a restore on 1.0.0 or 1.0.1 did, so a board can still carry
    // it; say so, since the absence of "Held" is all it shows otherwise.
    const bool spelled = !g_cfg.sysopDefault && !strcmp(g_cfg.sysopPass, BBS_DEFAULT_SYSOP);
    plat::log("cfg: sysop %s  co1 %s perms 0x%03x  co2 %s perms 0x%03x",     // never the passwords
              g_cfg.sysopDefault ? "on the published default, local network only"
                                 : spelled ? "on the PUBLISHED password, from anywhere: set yours in CONFIG staff"
                                 : g_cfg.sysopPass[0] ? "on" : "off",
              g_cfg.coPass[0][0] ? "on" : "off", g_cfg.coPerms[0],
              g_cfg.coPass[1][0] ? "on" : "off", g_cfg.coPerms[1]);
}

} // namespace

namespace syscfg {

// ---------------------------------------------------------------------------
// parseFile: key=value lines and the [access] section
// ---------------------------------------------------------------------------
// useDefaultSysop: no sysop_password line anywhere, so the board is fresh
// and the published default stands in (see BBS_DEFAULT_SYSOP).
static void useDefaultSysop(SysConfig& out) {
    copyStr(out.sysopPass, sizeof(out.sysopPass), BBS_DEFAULT_SYSOP);
    out.sysopDefault = true;
}

int parseFile(const char* path, SysConfig& out, char* err, size_t errLen) {
    Ctx c{ &out, 0, err, errLen, 0 };
    if (err && errLen) err[0] = '\0';
    FILE* f = fopen(path, "r");
    if (!f) {                                       // no file: defaults, not a problem
        useDefaultSysop(out);
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
    if (!sawSysop) useDefaultSysop(out);
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
    return true;
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

const char* pinProblem(long pin) {
    if (pin >= 6 && pin <= 11) return "pins 6-11 are the flash chip";
    return nullptr;
}

const char* trial(const KeyVal* pairs, uint8_t count, char* why, size_t n) {
    g_scratch = g_cfg;
    Ctx c{ &g_scratch, 0, why, n, 0 };
    c.bare = true;
    if (why && n) why[0] = '\0';
    char val[128];
    for (uint8_t i = 0; i < count; ++i) {
        const char* key = pairs[i].key;
        const char* v   = pairs[i].value;
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
    return crossCheck(c, g_scratch);
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
                    if (done[i] || !sameKey(line, pairs[i].key)) continue;
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
    remove(path);
    if (rename(tmp, path) != 0) {
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
