/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/users.cpp
 * Module:       Core / user accounts
 *
 * Purpose:      User account store (see users.h).
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     USERS.md
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

#include "users.h"
#include "disk.h"              // fopen and opendir that tell the drive light (1.1.1)
#include "sha256.h"
#include "sysconfig.h"
#include "bbs_util.h"                  // bbsu::foldHash, for the duplicate check
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <sys/stat.h>

const UserField kUserFields[] = {
    { "name",    "Name",    offsetof(UserRec, name),    sizeof(UserRec::name),    UF_REQUIRED },
    { "email",   "Email",   offsetof(UserRec, email),   sizeof(UserRec::email),   UF_REQUIRED | UF_PRIVATE | UF_EMAIL,
      "Email address" },
    // "From", not "Address" (Rob). A town and a country, not a postal
    // address: nobody should be typing their street into a board that says
    // in its own sign-up screen that nothing here is encrypted, and the
    // field has been asking for the wrong thing since it existed. The
    // stored key stays "address" so every users.txt already written keeps
    // working; only what a caller is asked for changes.
    // At 80 columns the label has room to say which of the two it means.
    { "address", "From",    offsetof(UserRec, address), sizeof(UserRec::address), UF_PRIVATE,
      "From (town, country)" },
    { "phone",   "Phone",   offsetof(UserRec, phone),   sizeof(UserRec::phone),   UF_PRIVATE },
    { "profile", "Profile", offsetof(UserRec, profile), sizeof(UserRec::profile), UF_TEXTAREA },
};
const uint8_t kUserFieldCount = sizeof(kUserFields) / sizeof(kUserFields[0]);

// The landing names, in one place, because the file, the form and the
// config page all have to agree on them and three copies would not.
namespace {
const char* const kLandKeys[]  = { "default", "main", "chat", "forums" };
const char* const kLandVerbs[] = { nullptr,   nullptr, "CHAT", "FORUMS" };
}
const char* users::landKey(uint8_t v) { return kLandKeys[v < 4 ? v : 0]; }
const char* users::landVerb(uint8_t v) { return kLandVerbs[v < 4 ? v : 0]; }

uint8_t users::landFromKey(const char* s) {
    if (!s || !*s) return LAND_DEFAULT;
    // "bulletin" was what this was called for one unreleased build. Accept
    // it so an account written by that build keeps the landing its owner
    // chose, instead of silently reverting to the board default, which is
    // the kind of quiet loss nobody thinks to look for. It is never written
    // back: the next save records "forums".
    {
        const char* a = s; const char* b = "bulletin";
        while (*a && *b && toupper(static_cast<unsigned char>(*a)) ==
                           toupper(static_cast<unsigned char>(*b))) { ++a; ++b; }
        if (!*a && !*b) return LAND_FORUMS;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        const char* a = s; const char* b = kLandKeys[i];
        while (*a && *b && toupper(static_cast<unsigned char>(*a)) ==
                           toupper(static_cast<unsigned char>(*b))) { ++a; ++b; }
        if (!*a && !*b) return i;
    }
    return LAND_DEFAULT;
}

namespace {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
bool ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) != toupper(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

char* trim(char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) --n;
    s[n] = '\0';
    return s;
}

void path(char* out, size_t n, const char* suffix) {
    snprintf(out, n, "%s/%s%s", plat::userBase(), BBS_USERS_FILE, suffix);
}

void toHex(const uint8_t* d, size_t n, char* out) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[i * 2]     = kHex[d[i] >> 4];
        out[i * 2 + 1] = kHex[d[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

bool fromHex(const char* s, uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint8_t v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = s[i * 2 + k];
            uint8_t d;
            if (c >= '0' && c <= '9')      d = static_cast<uint8_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint8_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<uint8_t>(c - 'A' + 10);
            else return false;
            v = static_cast<uint8_t>((v << 4) | d);
        }
        out[i] = v;
    }
    return true;
}

// hashPassword: h = SHA256(salt || pw), then h = SHA256(h || salt) repeated
void hashPassword(const uint8_t salt[8], const char* pw, uint8_t out[32]) {
    Sha256 s;
    s.update(salt, 8);
    s.update(reinterpret_cast<const uint8_t*>(pw), strlen(pw));
    s.finish(out);
    uint8_t buf[40];
    for (int r = 1; r < BBS_PASS_ROUNDS; ++r) {
        memcpy(buf, out, 32);
        memcpy(buf + 32, salt, 8);
        Sha256::digest(buf, sizeof(buf), out);
    }
}

// escape / unescape: backslash and newline in one-line values
void writeEscaped(FILE* f, const char* v) {
    for (; *v; ++v) {
        if (*v == '\\')      fputs("\\\\", f);
        else if (*v == '\n') fputs("\\n", f);
        else if (*v == '\r') continue;
        else                 fputc(*v, f);
    }
}

// unescapedLen: characters copyUnescaped would produce, without the cap
size_t unescapedLen(const char* src) {
    size_t n = 0;
    for (; *src; ++src) {
        if (*src == '\\' && src[1]) ++src;
        ++n;
    }
    return n;
}

// validHash: 16 hex, '$', 64 hex
bool validHash(const char* p) {
    if (strlen(p) != 81 || p[16] != '$') return false;
    for (size_t i = 0; i < 81; ++i) {
        if (i == 16) continue;
        char c = p[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

void copyUnescaped(char* dst, size_t cap, const char* src) {
    size_t n = 0;
    for (; *src && n + 1 < cap; ++src) {
        if (*src == '\\' && src[1]) {
            ++src;
            dst[n++] = *src == 'n' ? '\n' : *src;
        } else {
            dst[n++] = *src;
        }
    }
    dst[n] = '\0';
}

// ---------------------------------------------------------------------------
// Reader: streams blocks out of users.txt
// ---------------------------------------------------------------------------
class Reader {
public:
    explicit Reader(FILE* f) : f_(f) {}

    // next: parse the next block into u. Pass Issues to collect problems
    // (reject the file) and warnings (accept, but say what was dropped).
    // Values keep their spaces: only the one space after "= " is eaten.
    bool next(UserRec& u, users::Issues* iss = nullptr) {
        u = UserRec();
        bool inBlock = false;
        while (true) {
            if (!pending_) {
                lineAt_ = ftell(f_);
                if (!fgets(line_, sizeof(line_), f_)) {
                    if (iss && ferror(f_)) problem(iss, "read error");
                    return inBlock;
                }
                ++lineNo_;
            }
            pending_ = false;
            size_t n = strlen(line_);
            while (n && (line_[n - 1] == '\n' || line_[n - 1] == '\r')) line_[--n] = '\0';
            char* l = line_;
            while (*l == ' ' || *l == '\t') ++l;
            if (!*l || *l == '#') continue;
            if (*l == '[') {
                if (inBlock) { pending_ = true; return true; }      // next block starts
                blockAt_ = lineAt_;
                char* end = strchr(l, ']');
                if (!end) { problem(iss, "bad [handle] line"); continue; }
                *end = '\0';
                char* h = trim(l + 1);
                if (iss && strlen(h) > BBS_USER_MAX) problem(iss, "handle over 20 characters");
                strncpy(u.handle, h, BBS_USER_MAX);
                inBlock = true;
                continue;
            }
            if (!inBlock) { problem(iss, "key outside a [handle] block"); continue; }
            char* eq = strchr(l, '=');
            if (!eq) { problem(iss, "expected key = value"); continue; }
            *eq = '\0';
            char* key = trim(l);
            char* val = eq + 1;
            if (*val == ' ') ++val;                                 // the writer's "key = value"
            assign(u, key, val, iss);
        }
    }

    int lineNo() const { return lineNo_; }
    // blockAt: where the block next() last returned starts in the file, for
    // the handle index (1.1.2).
    long blockAt() const { return blockAt_; }

private:
    void problem(users::Issues* iss, const char* what) {
        if (!iss) return;
        if (!iss->problems && iss->err && iss->errLen) {
            snprintf(iss->err, iss->errLen, "users.txt line %d: %s", lineNo_, what);
        }
        ++iss->problems;
    }

    void warn(users::Issues* iss, const char* what, const char* detail) {
        if (!iss) return;
        if (!iss->warnings && iss->warn && iss->warnLen) {
            snprintf(iss->warn, iss->warnLen, "line %d: %s '%.20s'", lineNo_, what, detail);
        }
        ++iss->warnings;
    }

    // number: digits only, within range
    bool number(users::Issues* iss, const char* key, const char* val, uint32_t max, uint32_t& out) {
        char* end = nullptr;
        unsigned long v = strtoul(val, &end, 10);
        if (end == val || (end && *end) || v > max) {
            char what[40];
            snprintf(what, sizeof(what), "%.16s is not a number", key);
            problem(iss, what);
            return false;
        }
        out = static_cast<uint32_t>(v);
        return true;
    }

    void assign(UserRec& u, const char* key, const char* val, users::Issues* iss) {
        uint32_t n = 0;
        for (uint8_t i = 0; i < kUserFieldCount; ++i) {
            if (!strcmp(key, kUserFields[i].key)) {
                char* dst = users::fieldPtr(u, kUserFields[i]);
                copyUnescaped(dst, kUserFields[i].size, val);
                if (iss && unescapedLen(val) >= kUserFields[i].size) {
                    char what[48];
                    snprintf(what, sizeof(what), "%.16s is longer than %u characters",
                             key, static_cast<unsigned>(kUserFields[i].size - 1));
                    problem(iss, what);
                }
                return;
            }
        }
        if (!strcmp(key, "pass")) {
            if (strlen(val) >= sizeof(u.pass)) problem(iss, "pass is too long");
            strncpy(u.pass, val, sizeof(u.pass) - 1);
            if (iss && u.pass[0] && !validHash(u.pass)) {
                problem(iss, "pass is not a salt$hash (set passwords on the BBS)");
            }
        }
        else if (!strcmp(key, "created"))     { if (number(iss, key, val, 0xFFFFFFFFu, n)) u.created = n; }
        else if (!strcmp(key, "last_call"))   { if (number(iss, key, val, 0xFFFFFFFFu, n)) u.lastCall = n; }
        else if (!strcmp(key, "calls"))       { if (number(iss, key, val, 0xFFFFu, n)) u.calls = static_cast<uint16_t>(n); }
        else if (!strcmp(key, "day"))         { if (number(iss, key, val, 0xFFFFFFFFu, n)) u.dayKey = n; }
        else if (!strcmp(key, "day_minutes")) { if (number(iss, key, val, 0xFFFFu, n)) u.dayMinutes = static_cast<uint16_t>(n); }
        else if (!strcmp(key, "locked"))      { u.locked = ieq(val, "yes") || !strcmp(val, "1"); }
        else if (!strcmp(key, "land"))        { u.land = users::landFromKey(val); }
        else if (!strcmp(key, "id"))          { if (number(iss, key, val, 0xFFFFFFFFu, n)) u.id = n; }
        else if (!strcmp(key, "retired"))     { u.retired = ieq(val, "yes") || !strcmp(val, "1"); }
        else if (!strcmp(key, "staff_at"))    { if (number(iss, key, val, 0xFFFFFFFFu, n)) u.staffAt = n; }
        else if (!strcmp(key, "staff_level")) { if (number(iss, key, val, 3, n)) u.staffLevel = static_cast<uint8_t>(n); }
        else if (!strcmp(key, "staff_ip"))    { snprintf(u.staffIp, sizeof(u.staffIp), "%s", val); }
        else if (!strcmp(key, "level")) {
            if      (ieq(val, "sysop")) u.level = static_cast<uint8_t>(Access::Sysop);
            else if (ieq(val, "co1"))   u.level = static_cast<uint8_t>(Access::CoSysop1);
            else if (ieq(val, "co2"))   u.level = static_cast<uint8_t>(Access::CoSysop2);
            else if (ieq(val, "user") || !*val) u.level = 0;
            else problem(iss, "level must be user, co2, co1 or sysop");
        }
        else warn(iss, "unknown key", key);          // dropped when the file is next written
    }

    FILE* f_;
    char  line_[400] = {};
    bool  pending_ = false;
    int   lineNo_  = 0;
    long  lineAt_  = 0;
    long  blockAt_ = 0;
};

// ===========================================================================
// The handle index (1.1.2).
//
// Every question about an account used to be a parse of users.txt from the
// top: the handle prompt asked up to three times for an unknown handle (is
// it there, how many are there, twice), which on 250 accounts is about 112 KB
// of parsing a question, asked by anybody who connects, as often as they like
// (internal/audit-1.1.2-2026-09-26.md, item 7). Now one parse builds a table
// of each account's folded handle and where its block starts, and a lookup
// is a seek and one block; an unknown handle opens nothing. The count and
// the highest id come with it.
//
// Kept in step by the one writer: rewrite() lays the new file's offsets in
// as it writes, and a rewrite that fails leaves the index marked stale, so
// the next question rebuilds it rather than trusting it. Anything else that
// puts a users.txt in place (a restore) calls users::reindex().
//
// On the heap, taken once at the first build: 12 bytes an account, 3 KB
// for 250, which is static DRAM the camera boards do not have. The id is in
// it too, so the sysop's account, found by id, is a seek and not a walk. A file holding
// more blocks than that (only by hand) is answered the old way, by parsing.
// ===========================================================================
struct Ent { uint32_t hash; uint32_t off; uint32_t id; };
constexpr uint16_t kIdxMax = BBS_MAX_USERS + 16;
Ent*     g_ent   = nullptr;
uint16_t g_n     = 0;
uint32_t g_maxId = 0;
enum class Idx : uint8_t { Stale, Ok, Missing, Error, Over };
Idx      g_idx   = Idx::Stale;

// ===========================================================================
// Call statistics (1.1.2): calls, last_call, day and day_minutes, out of
// users.txt and into a file of their own, one 16-byte record an account at
// its id: <userdata>/callstats.dat.
//
// They changed at every logoff, and users.txt is one text file: counting a
// call meant parsing every account and writing them all back through a temp
// file and a rename. On 250 accounts that is about 112 KB and 28 block
// erases on every hang-up, each erase stopping both cores. A record in place
// is one block's copy on LittleFS, which commits a file's change whole at
// close (so a power cut leaves the old record or the new one, never half).
//
// users.txt still carries the four keys, as they stood at its last rewrite,
// so an older firmware reads it as ever. The first boot of this firmware
// copies them across (statsMigrate); a restore of a zip that has no
// callstats.dat does the same from the users.txt it restored. Everything
// that reads an account through this file gets the record's figures laid
// over the block's (statsOverlay), so nothing that shows them had to change.
// ===========================================================================
struct StatRec {
    uint32_t id;           // whose record this is: 0 is a hole, never written
    uint32_t lastCall;
    uint32_t dayKey;
    uint16_t calls;
    uint16_t dayMinutes;
};
static_assert(sizeof(StatRec) == 16, "StatRec is the file's record: 16 bytes");

void statsPath(char* out, size_t n, const char* suffix = "") {
    snprintf(out, n, "%s/%s%s", plat::userBase(), BBS_STATS_FILE, suffix);
}

// statsFrom: this account's record out of an open stats file, laid over the
// block's figures. A hole, a record for another id or a short file leaves
// them as the block had them.
void statsFrom(FILE* f, UserRec& u) {
    if (!f || !u.id) return;
    StatRec r;
    if (fseek(f, static_cast<long>(u.id) * static_cast<long>(sizeof(StatRec)), SEEK_SET) != 0) return;
    if (fread(&r, sizeof(r), 1, f) != 1 || r.id != u.id) return;
    u.lastCall   = r.lastCall;
    u.dayKey     = r.dayKey;
    u.calls      = r.calls;
    u.dayMinutes = r.dayMinutes;
}

FILE* statsOpen() {
    char p[96];
    statsPath(p, sizeof(p));
    return disk::open(p, "rb");
}

void statsOverlay(UserRec& u) {
    FILE* f = statsOpen();
    if (!f) return;
    statsFrom(f, u);
    fclose(f);
}

void writeRecord(FILE* f, const UserRec& u) {
    fprintf(f, "[%s]\n", u.handle);
    for (uint8_t i = 0; i < kUserFieldCount; ++i) {
        fprintf(f, "%s = ", kUserFields[i].key);
        writeEscaped(f, users::fieldPtr(u, kUserFields[i]));
        fputc('\n', f);
    }
    static const char* kLevels[] = { "user", "co2", "co1", "sysop" };
    fprintf(f, "pass = %s\nlevel = %s\ncreated = %u\nlast_call = %u\ncalls = %u\nday = %u\n"
               "day_minutes = %u\nlocked = %s\nland = %s\nid = %u\nretired = %s\n"
               "staff_at = %u\nstaff_level = %u\nstaff_ip = %s\n\n",
            u.pass, kLevels[u.level < 4 ? u.level : 0],
            static_cast<unsigned>(u.created), static_cast<unsigned>(u.lastCall),
            static_cast<unsigned>(u.calls), static_cast<unsigned>(u.dayKey),
            static_cast<unsigned>(u.dayMinutes), u.locked ? "yes" : "no",
            users::landKey(u.land), static_cast<unsigned>(u.id),
            u.retired ? "yes" : "no",
            static_cast<unsigned>(u.staffAt), static_cast<unsigned>(u.staffLevel),
            u.staffIp);
}

// ---------------------------------------------------------------------------
// rewrite: copy every block to a temp file, replacing or dropping one and
// optionally appending one, then rename over users.txt
// ---------------------------------------------------------------------------
// indexPut: one block of the file rewrite() is writing, at the offset it is
// about to be written at. Past the table's room the index says so, and the
// accounts are answered the old way until it is rebuilt.
uint16_t g_newN = 0;
uint32_t g_newMax = 0;
bool     g_newOver = false;

void indexPut(FILE* out, const UserRec& u) {
    if (!g_ent) return;
    if (g_newN >= kIdxMax) { g_newOver = true; return; }
    g_ent[g_newN].hash = bbsu::foldHash(u.handle);
    g_ent[g_newN].off  = static_cast<uint32_t>(ftell(out));
    g_ent[g_newN].id   = u.id;
    ++g_newN;
    if (u.id > g_newMax) g_newMax = u.id;
}

users::Result rewrite(const char* replaceHandle, const UserRec* replacement, const UserRec* append) {
    char live[96], tmp[96];
    path(live, sizeof(live), "");
    path(tmp, sizeof(tmp), ".new");
    FILE* out = disk::open(tmp, "w");
    if (!out) return users::Result::IoError;
    fprintf(out, "# %s users. Edit through the backup zip, see USERS.md.\n\n", BBS_NAME);
    // The index is rebuilt as the new file is written, so a lookup after a
    // rewrite costs a seek, not a parse. Stale until the rename has put the
    // new file live: a failure anywhere below leaves it stale, and the next
    // question rebuilds it from whichever file is live.
    g_idx     = Idx::Stale;
    g_newN    = 0;
    g_newMax  = 0;
    g_newOver = false;

    bool found = false;
    FILE* in = disk::open(live, "r");
    if (!in && errno != ENOENT) {                    // readable but not openable: never overwrite
        fclose(out);
        remove(tmp);
        return users::Result::IoError;
    }
    if (in) {
        // On the stack since 1.1.0, like every UserRec scratch here: they
        // were static "to keep them off the task stack", which cost 492
        // bytes of static DRAM apiece on a board with 4 KB of it left. This
        // one is on the deepest path the accounts have (a caller's record,
        // then this, then the fprintf into LittleFS), which is why the task
        // stack went to 12,288 first.
        UserRec u;
        Reader r(in);
        while (r.next(u)) {
            if (replaceHandle && ieq(u.handle, replaceHandle)) {
                found = true;
                if (replacement) { indexPut(out, *replacement); writeRecord(out, *replacement); }
                continue;
            }
            indexPut(out, u);
            writeRecord(out, u);
        }
        bool readFailed = ferror(in) != 0;
        fclose(in);
        if (readFailed) {                            // a half-read file would lose accounts
            fclose(out);
            remove(tmp);
            return users::Result::IoError;
        }
    }
    if (append) { indexPut(out, *append); writeRecord(out, *append); }
    bool ok = !ferror(out);
    ok = (fclose(out) == 0) && ok;
    if (!ok) { remove(tmp); return users::Result::IoError; }   // keep the live file as it was
    if (replaceHandle && !found) { remove(tmp); return users::Result::NotFound; }
    // Renamed over the live file, and nothing else (1.1.2). This fell back to
    // removing the live file and renaming again, the shape CLAUDE.md forbids:
    // users.txt is on LittleFS, which replaces a file by rename in one step,
    // so a rename here fails only for a reason that makes the second one fail
    // too (no space, a file held open), and the fallback then deleted every
    // account. Only FatFs refuses to rename over a file, and this file is
    // never on the card. A failed rename keeps the live file as it was and
    // the new one beside it for a sysop to look at.
    if (rename(tmp, live) != 0) {
        plat::log("users: users.txt could not be replaced (errno %d); kept as it was, %s beside it",
                  errno, BBS_USERS_FILE ".new");
        return users::Result::IoError;
    }
    if (g_ent && !g_newOver) {
        g_n     = g_newN;
        g_maxId = g_newMax;
        g_idx   = Idx::Ok;
    }
    return users::Result::Ok;
}

// indexBuild: the whole file parsed once, for the index. Loop only, and the
// only full parse left on a lookup's path: at boot, and after a restore.
Idx indexBuild() {
    if (!g_ent) {
        g_ent = static_cast<Ent*>(malloc(sizeof(Ent) * kIdxMax));
        if (!g_ent) return Idx::Over;                // no room for it: the old way
    }
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = disk::open(p, "r");
    g_n = 0;
    g_maxId = 0;
    if (!f) return errno == ENOENT ? Idx::Missing : Idx::Error;
    UserRec u;
    Reader r(f);
    Idx out = Idx::Ok;
    while (r.next(u)) {
        if (g_n >= kIdxMax) { out = Idx::Over; break; }
        g_ent[g_n].hash = bbsu::foldHash(u.handle);
        g_ent[g_n].off  = static_cast<uint32_t>(r.blockAt());
        g_ent[g_n].id   = u.id;
        ++g_n;
        if (u.id > g_maxId) g_maxId = u.id;
    }
    if (ferror(f)) out = Idx::Error;
    fclose(f);
    return out;
}

// indexReady: the index, built if it has gone stale. Ok and Missing are
// answers; Error and Over mean "ask the file".
Idx indexReady() {
    if (g_idx == Idx::Stale) g_idx = indexBuild();
    return g_idx;
}

// blockAt: the account whose block starts at off, parsed. False when the
// file does not hold a block there (the index is stale by then: rebuild).
bool blockAt(FILE* f, uint32_t off, UserRec& out) {
    if (fseek(f, static_cast<long>(off), SEEK_SET) != 0) return false;
    Reader r(f);
    return r.next(out);
}

} // namespace

namespace users {

bool validHandle(const char* h) {
    size_t n = strlen(h);
    if (!n || n > BBS_USER_MAX || !isalnum(static_cast<unsigned char>(h[0])) || h[n - 1] == ' ') return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(h[i]);
        if (!isalnum(c) && c != ' ' && c != '-' && c != '_' && c != '.') return false;
    }
    return !ieq(h, "SYSOP");
}

bool validEmail(const char* e) {
    const char* at = strchr(e, '@');
    if (!at || at == e || strchr(at + 1, '@') || strchr(e, ' ')) return false;
    const char* dot = strrchr(at, '.');
    return dot && dot > at + 1 && dot[1];
}

char* fieldPtr(UserRec& u, const UserField& f) {
    return reinterpret_cast<char*>(&u) + f.offset;
}

const char* fieldPtr(const UserRec& u, const UserField& f) {
    return reinterpret_cast<const char*>(&u) + f.offset;
}

void reindex() {
    g_idx = Idx::Stale;
}

// lookupSlow: the whole file, as before the index. For a file the index
// could not hold or read.
Lookup lookupSlow(const char* handle, UserRec& out) {
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = disk::open(p, "r");
    if (!f) return errno == ENOENT ? Lookup::Missing : Lookup::Error;
    Reader r(f);
    bool found = false;
    while (r.next(out)) {
        if (ieq(out.handle, handle)) { found = true; break; }
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    if (found) { statsOverlay(out); return Lookup::Found; }
    return bad ? Lookup::Error : Lookup::Missing;
}

Lookup lookup(const char* handle, UserRec& out) {
    const Idx x = indexReady();
    if (x == Idx::Missing) return Lookup::Missing;
    if (x != Idx::Ok) return lookupSlow(handle, out);
    const uint32_t h = bbsu::foldHash(handle);
    FILE* f = nullptr;
    for (uint16_t i = 0; i < g_n; ++i) {
        if (g_ent[i].hash != h) continue;           // an unknown handle opens nothing
        if (!f) {
            char p[96];
            path(p, sizeof(p), "");
            f = disk::open(p, "r");
            if (!f) { g_idx = Idx::Stale; return errno == ENOENT ? Lookup::Missing : Lookup::Error; }
        }
        if (!blockAt(f, g_ent[i].off, out)) { fclose(f); g_idx = Idx::Stale; return lookupSlow(handle, out); }
        if (ieq(out.handle, handle)) {
            fclose(f);
            statsOverlay(out);
            return Lookup::Found;
        }
    }
    if (f) fclose(f);
    return Lookup::Missing;
}

bool find(const char* handle, UserRec& out) {
    return lookup(handle, out) == Lookup::Found;
}

// exists: never inlined, so the record lives only for the length of the
// question. add() and update() ask it and then rewrite the whole file;
// inlined, the record would sit on the stack under rewrite()'s own for the
// entire write, 492 bytes doing nothing on the deepest path the accounts
// have.
__attribute__((noinline)) bool exists(const char* handle) {
    UserRec probe;
    return find(handle, probe);
}

// byId: the account with this id, from the index: a seek and one block
// (1.1.2). The old way with no index: a walk.
bool byId(uint32_t id, UserRec& out) {
    if (!id) return false;
    char p[96];
    path(p, sizeof(p), "");
    if (indexReady() == Idx::Ok) {
        for (uint16_t i = 0; i < g_n; ++i) {
            if (g_ent[i].id != id) continue;
            FILE* f = disk::open(p, "r");
            if (!f) return false;
            const bool ok = blockAt(f, g_ent[i].off, out) && out.id == id;
            fclose(f);
            if (!ok) { g_idx = Idx::Stale; return false; }
            statsOverlay(out);
            return true;
        }
        return false;
    }
    FILE* f = disk::open(p, "r");
    if (!f) return false;
    Reader r(f);
    bool found = false;
    while (r.next(out)) if (out.id == id) { found = true; break; }
    fclose(f);
    if (found) statsOverlay(out);
    return found;
}

uint8_t count() {
    // The index's count (1.1.2): the handle prompt asked twice a caller.
    if (indexReady() == Idx::Ok)      return static_cast<uint8_t>(g_n > 255 ? 255 : g_n);
    if (g_idx == Idx::Missing)        return 0;
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = disk::open(p, "r");
    if (!f) return 0;
    UserRec u;
    Reader r(f);
    uint16_t n = 0;
    while (r.next(u)) ++n;
    fclose(f);
    return static_cast<uint8_t>(n > 255 ? 255 : n);
}

bool at(uint8_t index, UserRec& out) {
    char p[96];
    path(p, sizeof(p), "");
    if (indexReady() == Idx::Ok) {                  // a seek, not a walk (1.1.2)
        if (index >= g_n) return false;
        FILE* f = disk::open(p, "r");
        if (!f) return false;
        const bool ok = blockAt(f, g_ent[index].off, out);
        fclose(f);
        if (!ok) { g_idx = Idx::Stale; return false; }
        statsOverlay(out);
        return true;
    }
    FILE* f = disk::open(p, "r");
    if (!f) return false;
    Reader r(f);
    uint16_t i = 0;
    bool ok = false;
    while (r.next(out)) {
        if (i++ == index) { ok = true; break; }
    }
    fclose(f);
    if (ok) statsOverlay(out);
    return ok;
}

uint8_t range(uint8_t start, uint8_t n, RangeFn fn, void* ctx) {
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = disk::open(p, "r");
    if (!f) return 0;
    FILE* st = statsOpen();              // one open for the range, not one a row
    UserRec u;                           // fn gets a reference for the call only
    uint16_t i = 0;
    // From the index, the range starts where its first block does rather
    // than at the top of the file (1.1.2): a page of the user list no
    // longer walks every page before it.
    if (indexReady() == Idx::Ok && start < g_n && fseek(f, static_cast<long>(g_ent[start].off), SEEK_SET) == 0)
        i = start;
    Reader r(f);
    uint8_t done = 0;
    while (done < n && r.next(u)) {
        if (i >= start) { statsFrom(st, u); fn(ctx, static_cast<uint8_t>(i), u); ++done; }
        ++i;
    }
    if (st) fclose(st);
    fclose(f);
    return done;
}

Result add(const UserRec& u) {
    if (exists(u.handle)) return Result::Exists;
    if (count() >= syscfg::get().maxUsers) return Result::Full;

    // An account is never written without an id. The caller may already
    // have set one (the migration does); otherwise it gets the next.
    //
    // The invariant this protects: an id is never used anywhere until the
    // record carrying it is on disk. rewrite() builds a temp file and only
    // renames on success, so a failure here means the id was never issued
    // rather than issued twice.
    UserRec withId = u;
    if (!withId.id) withId.id = maxId() + 1;
    return rewrite(nullptr, nullptr, &withId);
}

// maxId / assignIds: see users.h for why the counter is derived.
uint32_t maxId() {
    if (indexReady() == Idx::Ok) return g_maxId;    // kept by the index (1.1.2)
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = disk::open(p, "r");
    if (!f) return 0;
    UserRec u;
    Reader r(f);
    uint32_t hi = 0;
    while (r.next(u)) if (u.id > hi) hi = u.id;
    fclose(f);
    return hi;
}

uint8_t assignIds() {
    uint32_t next = maxId();
    uint8_t  gave = 0;
    for (uint8_t i = 0; i < count(); ++i) {
        UserRec u;
        if (!at(i, u) || u.id) continue;
        u.id = ++next;
        if (update(u.handle, u) != Result::Ok) {
            plat::log("users: could not give '%s' an id", u.handle);
            break;                       // stop rather than skip: ids must be dense-ish
                                         // and a failing file will not fix itself
        }
        if (gave < 0xFF) ++gave;
    }
    if (gave) plat::log("users: assigned %u account id%s", static_cast<unsigned>(gave),
                        gave == 1 ? "" : "s");
    return gave;
}

Result update(const char* originalHandle, const UserRec& u) {
    if (!ieq(originalHandle, u.handle) && exists(u.handle)) {  // renamed: the new name must be free
        return Result::Exists;
    }
    return rewrite(originalHandle, &u, nullptr);
}

// retire: the account stops working, the identity stays.
//
// This is what USER DEL does now. remove() is still here for the one case
// that genuinely wants the block gone, and it is deliberately not reachable
// from a caller-facing command.
Result retire(const char* handle) {
    UserRec u;
    if (!find(handle, u)) return Result::NotFound;
    if (u.retired) return Result::Ok;                 // already, and that is fine
    u.retired = true;
    u.locked  = true;                                 // belt and braces at the login path
    return update(handle, u);
}

// purge: blank what the board knows about a person, keep that they existed.
//
// For a sysop who genuinely wants somebody gone. The id, the handle and the
// retired flag stay, so nothing anywhere is orphaned and the handle is
// never reissued. Everything a person would recognise as theirs goes.
Result purge(const char* handle) {
    UserRec u;
    if (!find(handle, u)) return Result::NotFound;
    UserRec blank;
    blank.id      = u.id;                             // the identity survives
    blank.retired = true;
    blank.locked  = true;
    blank.created = u.created;
    snprintf(blank.handle, sizeof(blank.handle), "%s", u.handle);
    Result r = update(handle, blank);
    // Its call figures go too (1.1.2): they live in callstats.dat now, and a
    // purge that left them there would not have blanked what the board knows.
    if (r == Result::Ok) statsPut(u.id, 0, 0, 0, 0);
    return r;
}

Result remove(const char* handle) {
    return rewrite(handle, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// The call statistics' file (1.1.2): see statsFrom above.
// ---------------------------------------------------------------------------
bool statsPut(uint32_t id, uint16_t calls, uint32_t lastCall, uint32_t dayKey, uint16_t dayMinutes) {
    if (!id) return false;
    char p[96];
    statsPath(p, sizeof(p));
    // In place: r+ keeps every other record as it is. A new board's first
    // record makes the file. An id past its end leaves a hole of zeros,
    // which reads back as "no record" (id 0), which is the truth.
    FILE* f = disk::open(p, "r+b");
    // Made only when it is not there: w+b truncates, and an open that failed
    // for any other reason would have thrown every account's figures away.
    if (!f && errno == ENOENT) f = disk::open(p, "w+b");
    if (!f) return false;
    StatRec r{ id, lastCall, dayKey, calls, dayMinutes };
    bool ok = fseek(f, static_cast<long>(id) * static_cast<long>(sizeof(StatRec)), SEEK_SET) == 0 &&
              fwrite(&r, sizeof(r), 1, f) == 1;
    ok = (fclose(f) == 0) && ok;
    if (!ok) plat::diskPulse(plat::DISK_ERROR);
    return ok;
}

// statsMigrate: make callstats.dat from users.txt's figures, when there is
// none. The first boot of 1.1.2, and after a restore of a zip without one.
// Written beside the live name and renamed in, so a failure half way leaves
// no file and the next boot tries again. Returns how many it copied.
uint16_t statsMigrate() {
    char p[96], tmp[96];
    statsPath(p, sizeof(p));
    struct stat st;
    if (stat(p, &st) == 0) return 0;                  // there already: its figures win
    char up[96];
    path(up, sizeof(up), "");
    FILE* in = disk::open(up, "r");
    if (!in) return 0;                               // no accounts: the first record makes it
    statsPath(tmp, sizeof(tmp), ".new");
    FILE* out = disk::open(tmp, "w+b");
    if (!out) { fclose(in); return 0; }
    UserRec u;
    Reader r(in);
    uint16_t n = 0;
    bool ok = true;
    while (ok && r.next(u)) {
        if (!u.id) continue;
        StatRec rec{ u.id, u.lastCall, u.dayKey, u.calls, u.dayMinutes };
        ok = fseek(out, static_cast<long>(u.id) * static_cast<long>(sizeof(StatRec)), SEEK_SET) == 0 &&
             fwrite(&rec, sizeof(rec), 1, out) == 1;
        ++n;
    }
    ok = !ferror(in) && ok;
    fclose(in);
    ok = (fclose(out) == 0) && ok;
    if (!ok || rename(tmp, p) != 0) {
        ::remove(tmp);
        plat::log("users: could not make %s; the figures stay in users.txt", BBS_STATS_FILE);
        return 0;
    }
    plat::log("users: call figures for %u account%s moved to %s", static_cast<unsigned>(n),
              n == 1 ? "" : "s", BBS_STATS_FILE);
    return n;
}

void setPassword(UserRec& u, const char* password) {
    uint8_t salt[8];
    for (int i = 0; i < 8; i += 4) {
        uint32_t r = plat::random32();
        memcpy(salt + i, &r, 4);
    }
    uint8_t h[32];
    hashPassword(salt, password, h);
    toHex(salt, 8, u.pass);
    u.pass[16] = '$';
    toHex(h, 32, u.pass + 17);
}

bool checkPassword(const UserRec& u, const char* password) {
    uint8_t salt[8], want[32], got[32];
    if (strlen(u.pass) != 81 || u.pass[16] != '$') return false;
    if (!fromHex(u.pass, salt, 8) || !fromHex(u.pass + 17, want, 32)) return false;
    hashPassword(salt, password, got);
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= static_cast<uint8_t>(want[i] ^ got[i]);
    return diff == 0;
}

int validateFile(const char* p, Issues& iss) {
    if (iss.err && iss.errLen) iss.err[0] = '\0';
    if (iss.warn && iss.warnLen) iss.warn[0] = '\0';
    FILE* f = disk::open(p, "r");
    if (!f) {
        if (iss.err && iss.errLen) snprintf(iss.err, iss.errLen, "cannot read users.txt");
        return ++iss.problems;
    }
    UserRec u;
    // Hashes, not handles: this table is only ever asked "have I seen this
    // one already", which is an equality question, and 250 handles at 21
    // bytes was 5,250 bytes of static DRAM held for a check that runs when
    // somebody uploads a backup. 250 hashes is 1,000.
    //
    // Folded, because duplicate detection here has always been
    // case-insensitive (it used ieq).
    //
    // The trade, stated rather than buried: two different handles that hash
    // alike would be reported as duplicates and the upload refused. With 250
    // entries in a 32-bit space that is about one in 137,000 uploads, it
    // fails toward refusing a good backup rather than accepting a bad one,
    // and the sysop can rename and retry.
    static uint32_t seen[BBS_MAX_USERS];
    uint16_t n = 0;
    Reader r(f);
    while (r.next(u, &iss)) {
        int line = r.lineNo();
        auto fail = [&](const char* what) {
            if (!iss.problems && iss.err && iss.errLen) {
                snprintf(iss.err, iss.errLen, "users.txt line %d [%.20s]: %s", line, u.handle, what);
            }
            ++iss.problems;
        };
        if (!validHandle(u.handle))                     fail("invalid handle");
        if (u.pass[0] && !validHash(u.pass))            fail("pass is not a salt$hash (set passwords on the BBS)");
        uint32_t hh = bbsu::foldHash(u.handle);
        for (uint16_t k = 0; k < n && k < BBS_MAX_USERS; ++k)
            if (seen[k] == hh) fail("duplicate handle");
        if (n < BBS_MAX_USERS) seen[n] = hh;
        uint8_t maxUsers = iss.maxUsers ? iss.maxUsers : syscfg::get().maxUsers;
        if (++n > maxUsers) fail("more accounts than max_users");
    }
    fclose(f);
    iss.accounts = n;
    return iss.problems;
}

} // namespace users
