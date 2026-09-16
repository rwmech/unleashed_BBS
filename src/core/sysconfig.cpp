/*
 * File:        src/core/sysconfig.cpp
 * Description: system.cfg loader and staff access matrix (see sysconfig.h).
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
};
const uint8_t kPermCount = sizeof(kPermNames) / sizeof(kPermNames[0]);

namespace {

SysConfig g_cfg;   // static storage, filled once at boot

// ---------------------------------------------------------------------------
// trim: strip leading/trailing whitespace in place, return the start
// ---------------------------------------------------------------------------
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

uint16_t toMinutes(const char* v) {
    long n = strtol(v, nullptr, 10);
    if (n < 0) n = 0;
    if (n > 1440) n = 1440;
    return static_cast<uint16_t>(n);
}

bool ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) != toupper(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// ---------------------------------------------------------------------------
// cell: one matrix cell. 1 allowed, 0 denied, -1 unreadable.
// ---------------------------------------------------------------------------
int cell(const char* tok) {
    if (!tok) return -1;
    if (ieq(tok, "X") || ieq(tok, "Y") || ieq(tok, "YES")) return 1;
    if (!strcmp(tok, "-") || !strcmp(tok, ".") || ieq(tok, "N") || ieq(tok, "NO")) return 0;
    return -1;
}

// ---------------------------------------------------------------------------
// accessRow: "NAME SYSOP CO1 CO2"
// ---------------------------------------------------------------------------
void accessRow(char* line) {
    char* tok[4] = {};
    uint8_t n = 0;
    char* save = nullptr;
    for (char* p = strtok_r(line, " \t", &save); p && n < 4; p = strtok_r(nullptr, " \t", &save)) tok[n++] = p;
    if (n == 0) return;
    if (n < 4) { plat::log("cfg: [access] row '%s' needs SYSOP CO1 CO2 columns", tok[0]); return; }

    uint16_t bit = 0;
    for (uint8_t i = 0; i < kPermCount; ++i) {
        if (ieq(tok[0], kPermNames[i].name)) { bit = kPermNames[i].bit; break; }
    }
    if (!bit) { plat::log("cfg: [access] unknown permission '%s'", tok[0]); return; }

    if (cell(tok[1]) != 1) plat::log("cfg: [access] %s: SYSOP always has every permission", tok[0]);
    for (uint8_t c = 0; c < 2; ++c) {
        int v = cell(tok[2 + c]);
        if (v < 0) { plat::log("cfg: [access] %s: use X or - in CO%u", tok[0], c + 1); continue; }
        if (v) g_cfg.coPerms[c] = static_cast<uint16_t>(g_cfg.coPerms[c] | bit);
        else   g_cfg.coPerms[c] = static_cast<uint16_t>(g_cfg.coPerms[c] & ~bit);
    }
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

} // namespace

namespace syscfg {

// ---------------------------------------------------------------------------
// load: parse key=value lines and the [access] section, then apply TZ
// ---------------------------------------------------------------------------
bool load() {
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", plat::fsBase(), BBS_CONFIG_FILE);
    FILE* f = fopen(path, "r");
    if (f) {
        char line[128];
        bool inAccess = false;
        while (fgets(line, sizeof(line), f)) {
            char* hash = strchr(line, '#');
            if (hash) *hash = '\0';
            char* l = trim(line);
            if (!*l) continue;

            if (*l == '[') {
                inAccess = ieq(l, "[access]");
                if (!inAccess) plat::log("cfg: unknown section %s", l);
                continue;
            }
            if (inAccess) { accessRow(l); continue; }

            char* eq = strchr(l, '=');
            if (!eq) continue;
            *eq = '\0';
            char* key = trim(l);
            char* val = trim(eq + 1);

            if (!strcmp(key, "tz"))                     copyStr(g_cfg.tz, sizeof(g_cfg.tz), val);
            else if (!strcmp(key, "ntp_server"))        copyStr(g_cfg.ntpServer, sizeof(g_cfg.ntpServer), val);
            else if (!strcmp(key, "sysop_password"))    copyStr(g_cfg.sysopPass, sizeof(g_cfg.sysopPass), val);
            else if (!strcmp(key, "cosysop1_password")) copyStr(g_cfg.coPass[0], sizeof(g_cfg.coPass[0]), val);
            else if (!strcmp(key, "cosysop2_password")) copyStr(g_cfg.coPass[1], sizeof(g_cfg.coPass[1]), val);
            else if (!strcmp(key, "idle_minutes"))      g_cfg.idleMinutes = toMinutes(val);
            else if (!strcmp(key, "call_minutes"))      g_cfg.callMinutes = toMinutes(val);
            else if (!strcmp(key, "day_minutes"))       g_cfg.dayMinutes  = toMinutes(val);
            else plat::log("cfg: unknown key '%s'", key);
        }
        fclose(f);
        g_cfg.fromFile = true;
    }

    setenv("TZ", g_cfg.tz, 1);
    tzset();

    // never log the passwords themselves
    plat::log("cfg: %s  tz %s  ntp %s  idle %u  limits %u/call %u/day",
              g_cfg.fromFile ? BBS_CONFIG_FILE : "defaults (no " BBS_CONFIG_FILE ")",
              g_cfg.tz, g_cfg.ntpServer, g_cfg.idleMinutes, g_cfg.callMinutes, g_cfg.dayMinutes);
    plat::log("cfg: sysop %s  co1 %s perms 0x%03x  co2 %s perms 0x%03x",
              g_cfg.sysopPass[0] ? "on" : "off",
              g_cfg.coPass[0][0] ? "on" : "off", g_cfg.coPerms[0],
              g_cfg.coPass[1][0] ? "on" : "off", g_cfg.coPerms[1]);
    return g_cfg.fromFile;
}

const SysConfig& get() {
    return g_cfg;
}

// ---------------------------------------------------------------------------
// passwordLevel: compare all three so timing does not reveal which exist
// ---------------------------------------------------------------------------
Access passwordLevel(const char* candidate) {
    if (!candidate || !*candidate) return Access::None;
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

} // namespace syscfg
