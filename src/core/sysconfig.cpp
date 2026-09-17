/*
 * File:        src/core/sysconfig.cpp
 * Description: system.cfg loader, validator, password redaction and the
 *              staff access matrix (see sysconfig.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc stdio, stdlib, time)
 */
#include "sysconfig.h"
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

const char* const kPasswordKeys[] = { "sysop_password", "cosysop1_password", "cosysop2_password" };

// ---------------------------------------------------------------------------
// Parse context: where problems are counted and the first one kept
// ---------------------------------------------------------------------------
struct Ctx {
    SysConfig* cfg;
    int        problems;
    char*      err;
    size_t     errLen;
    int        lineNo;
};

void problem(Ctx& c, const char* what, const char* detail) {
    if (!c.problems && c.err && c.errLen) {
        snprintf(c.err, c.errLen, "system.cfg line %d: %s %s", c.lineNo, what, detail);
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

// number: whole decimal in [lo, hi], else a problem and false
bool number(Ctx& c, const char* key, const char* v, long lo, long hi, long& out) {
    char* end = nullptr;
    long n = strtol(v, &end, 10);
    if (!*v || (end && *end) || n < lo || n > hi) {
        char d[48];
        snprintf(d, sizeof(d), "(%ld..%ld)", lo, hi);
        problem(c, key, d);
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

// validHostname: a-z 0-9 -, 1..31, not starting or ending with '-'
bool validHostname(char* v) {
    size_t n = strlen(v);
    if (!n || n >= 32 || v[0] == '-' || v[n - 1] == '-') return false;
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<char>(tolower(static_cast<unsigned char>(v[i])));
        if (!isalnum(static_cast<unsigned char>(v[i])) && v[i] != '-') return false;
    }
    return true;
}

// keyValue: one "key = value" line
void keyValue(Ctx& c, char* key, char* val) {
    SysConfig& g = *c.cfg;
    long n = 0;
    if (!strcmp(key, "hostname")) {
        if (validHostname(val)) copyStr(g.hostname, sizeof(g.hostname), val);
        else problem(c, "hostname must be a-z 0-9 - (1..31):", val);
    }
    else if (!strcmp(key, "tz"))                     copyStr(g.tz, sizeof(g.tz), val);
    else if (!strcmp(key, "ntp_server"))             copyStr(g.ntpServer, sizeof(g.ntpServer), val);
    else if (!strcmp(key, "sysop_password"))         copyStr(g.sysopPass, sizeof(g.sysopPass), val);
    else if (!strcmp(key, "cosysop1_password"))      copyStr(g.coPass[0], sizeof(g.coPass[0]), val);
    else if (!strcmp(key, "cosysop2_password"))      copyStr(g.coPass[1], sizeof(g.coPass[1]), val);
    else if (!strcmp(key, "idle_minutes"))          { if (number(c, key, val, 0, 1440, n)) g.idleMinutes = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "call_minutes"))          { if (number(c, key, val, 0, 1440, n)) g.callMinutes = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "day_minutes"))           { if (number(c, key, val, 0, 1440, n)) g.dayMinutes  = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "backup_window_minutes")) { if (number(c, key, val, 1, 60, n)) g.backupMinutes = static_cast<uint16_t>(n); }
    else if (!strcmp(key, "backup_button_gpio"))    { if (number(c, key, val, -1, 39, n)) g.backupGpio = static_cast<int8_t>(n); }
    else if (!strcmp(key, "activity_led_gpio"))     { if (number(c, key, val, -1, 39, n)) g.ledGpio = static_cast<int8_t>(n); }
    else if (!strcmp(key, "max_users"))             { if (number(c, key, val, 1, BBS_MAX_USERS, n)) g.maxUsers = static_cast<uint8_t>(n); }
    else if (!strcmp(key, "self_register")) {
        if (ieq(val, "yes") || ieq(val, "on") || !strcmp(val, "1"))      g.selfRegister = true;
        else if (ieq(val, "no") || ieq(val, "off") || !strcmp(val, "0")) g.selfRegister = false;
        else problem(c, "self_register must be yes or no:", val);
    }
    else if (!strcmp(key, "who_refresh_min"))       { if (number(c, key, val, 1, 60, n)) g.whoMin = static_cast<uint8_t>(n); }
    else if (!strcmp(key, "who_refresh_max"))       { if (number(c, key, val, 1, 60, n)) g.whoMax = static_cast<uint8_t>(n); }
    else if (!strcmp(key, "backup_port")) {
        if (number(c, key, val, 1, 65535, n)) {
            if (n == BBS_PORT) problem(c, "backup_port cannot be the BBS port", val);
            else g.backupPort = static_cast<uint16_t>(n);
        }
    }
    else plat::log("cfg: line %d unknown key '%s' ignored", c.lineNo, key);
}

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
    char* hash = strchr(tmp, '#');
    if (hash) *hash = '\0';
    char* eq = strchr(tmp, '=');
    if (!eq) return false;
    *eq = '\0';
    char* key = trim(tmp);
    for (int i = 0; i < 3; ++i) {
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
    plat::log("cfg: who refresh %u..%u s  activity led gpio %d  self_register %s  max_users %u",
              g_cfg.whoMin, g_cfg.whoMax, g_cfg.ledGpio, g_cfg.selfRegister ? "yes" : "no", g_cfg.maxUsers);
    plat::log("cfg: sysop %s  co1 %s perms 0x%03x  co2 %s perms 0x%03x",     // never the passwords
              g_cfg.sysopPass[0] ? "on" : "off",
              g_cfg.coPass[0][0] ? "on" : "off", g_cfg.coPerms[0],
              g_cfg.coPass[1][0] ? "on" : "off", g_cfg.coPerms[1]);
}

} // namespace

namespace syscfg {

// ---------------------------------------------------------------------------
// parseFile: key=value lines and the [access] section
// ---------------------------------------------------------------------------
int parseFile(const char* path, SysConfig& out, char* err, size_t errLen) {
    Ctx c{ &out, 0, err, errLen, 0 };
    if (err && errLen) err[0] = '\0';
    FILE* f = fopen(path, "r");
    if (!f) return 0;                               // no file: defaults, not a problem

    char line[160];
    bool inAccess = false;
    while (fgets(line, sizeof(line), f)) {
        ++c.lineNo;
        if (!strchr(line, '\n') && !feof(f)) {      // overlong line: skip the rest of it
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {}
            problem(c, "line too long", "");
            continue;
        }
        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char* l = trim(line);
        if (!*l) continue;

        if (*l == '[') {
            inAccess = ieq(l, "[access]");
            if (!inAccess) problem(c, "unknown section", l);
            continue;
        }
        if (inAccess) { accessRow(c, l); continue; }

        char* eq = strchr(l, '=');
        if (!eq) { problem(c, "expected key = value:", l); continue; }
        *eq = '\0';
        char* key = trim(l);
        char* val = trim(eq + 1);
        if (!strcmp(val, "***")) continue;          // redacted password: keep what is set
        keyValue(c, key, val);
    }
    fclose(f);
    if (out.whoMin > out.whoMax) {
        c.lineNo = 0;
        problem(c, "who_refresh_min is above who_refresh_max", "");
        out.whoMin = out.whoMax;
    }
    out.fromFile = true;
    return c.problems;
}

bool load() {
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", plat::fsBase(), BBS_CONFIG_FILE);
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
    snprintf(path, sizeof(path), "%s/%s", plat::fsBase(), BBS_CONFIG_FILE);
    static SysConfig fresh;                         // static: SysConfig is ~300 bytes
    fresh = SysConfig();
    if (parseFile(path, fresh, err, errLen)) return false;
    g_cfg = fresh;
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

bool redactLine(const char* line, char* out, size_t outLen) {
    char tmp[160];
    int idx = 0;
    char* value = nullptr;
    if (!passwordAssignment(line, tmp, sizeof(tmp), idx, value) || !*value) return false;
    snprintf(out, outLen, "%s = ***\n", kPasswordKeys[idx]);
    return true;
}

bool unredactLine(const char* line, char* out, size_t outLen) {
    char tmp[160];
    int idx = 0;
    char* value = nullptr;
    if (!passwordAssignment(line, tmp, sizeof(tmp), idx, value) || strcmp(value, "***")) return false;
    snprintf(out, outLen, "%s = %s\n", kPasswordKeys[idx], livePassword(idx));
    return true;
}

} // namespace syscfg
