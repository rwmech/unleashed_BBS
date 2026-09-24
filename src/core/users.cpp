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
#include "sha256.h"
#include "sysconfig.h"
#include "bbs_util.h"                  // bbsu::foldHash, for the duplicate check
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>

const UserField kUserFields[] = {
    { "name",    "Name",    offsetof(UserRec, name),    sizeof(UserRec::name),    UF_REQUIRED },
    { "email",   "Email",   offsetof(UserRec, email),   sizeof(UserRec::email),   UF_REQUIRED | UF_PRIVATE | UF_EMAIL },
    // "From", not "Address" (Rob). A town and a country, not a postal
    // address: nobody should be typing their street into a board that says
    // in its own sign-up screen that nothing here is encrypted, and the
    // field has been asking for the wrong thing since it existed. The
    // stored key stays "address" so every users.txt already written keeps
    // working; only what a caller is asked for changes.
    { "address", "From",    offsetof(UserRec, address), sizeof(UserRec::address), UF_PRIVATE },
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
};

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
users::Result rewrite(const char* replaceHandle, const UserRec* replacement, const UserRec* append) {
    char live[96], tmp[96];
    path(live, sizeof(live), "");
    path(tmp, sizeof(tmp), ".new");
    FILE* out = fopen(tmp, "w");
    if (!out) return users::Result::IoError;
    fprintf(out, "# %s users. Edit through the backup zip, see USERS.md.\n\n", BBS_NAME);

    bool found = false;
    FILE* in = fopen(live, "r");
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
                if (replacement) writeRecord(out, *replacement);
                continue;
            }
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
    if (append) writeRecord(out, *append);
    bool ok = !ferror(out);
    ok = (fclose(out) == 0) && ok;
    if (!ok) { remove(tmp); return users::Result::IoError; }   // keep the live file as it was
    if (replaceHandle && !found) { remove(tmp); return users::Result::NotFound; }
    if (rename(tmp, live) != 0) {
        remove(live);                                // some filesystems will not rename over a file
        if (rename(tmp, live) != 0) return users::Result::IoError;   // tmp kept for recovery
    }
    return users::Result::Ok;
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

Lookup lookup(const char* handle, UserRec& out) {
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = fopen(p, "r");
    if (!f) return errno == ENOENT ? Lookup::Missing : Lookup::Error;
    Reader r(f);
    bool found = false;
    while (r.next(out)) {
        if (ieq(out.handle, handle)) { found = true; break; }
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    if (found) return Lookup::Found;
    return bad ? Lookup::Error : Lookup::Missing;
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

uint8_t count() {
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = fopen(p, "r");
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
    FILE* f = fopen(p, "r");
    if (!f) return false;
    Reader r(f);
    uint16_t i = 0;
    bool ok = false;
    while (r.next(out)) {
        if (i++ == index) { ok = true; break; }
    }
    fclose(f);
    return ok;
}

uint8_t range(uint8_t start, uint8_t n, RangeFn fn, void* ctx) {
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = fopen(p, "r");
    if (!f) return 0;
    UserRec u;                           // fn gets a reference for the call only
    Reader r(f);
    uint16_t i = 0;
    uint8_t done = 0;
    while (done < n && r.next(u)) {
        if (i >= start) { fn(ctx, static_cast<uint8_t>(i), u); ++done; }
        ++i;
    }
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
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = fopen(p, "r");
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
    return update(handle, blank);
}

Result remove(const char* handle) {
    return rewrite(handle, nullptr, nullptr);
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
    FILE* f = fopen(p, "r");
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
