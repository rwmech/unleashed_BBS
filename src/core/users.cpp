/*
 * File:        src/core/users.cpp
 * Description: User account store (see users.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc stdio)
 */
#include "users.h"
#include "sha256.h"
#include "sysconfig.h"
#include "../platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

const UserField kUserFields[] = {
    { "name",    "Name",    offsetof(UserRec, name),    sizeof(UserRec::name),    UF_REQUIRED },
    { "email",   "Email",   offsetof(UserRec, email),   sizeof(UserRec::email),   UF_REQUIRED | UF_PRIVATE | UF_EMAIL },
    { "address", "Address", offsetof(UserRec, address), sizeof(UserRec::address), UF_PRIVATE },
    { "phone",   "Phone",   offsetof(UserRec, phone),   sizeof(UserRec::phone),   UF_PRIVATE },
    { "profile", "Profile", offsetof(UserRec, profile), sizeof(UserRec::profile), UF_TEXTAREA },
};
const uint8_t kUserFieldCount = sizeof(kUserFields) / sizeof(kUserFields[0]);

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
    snprintf(out, n, "%s/%s%s", plat::fsBase(), BBS_USERS_FILE, suffix);
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

    // next: parse the next block into u. problem() text for validation.
    bool next(UserRec& u, int* problems = nullptr, char* err = nullptr, size_t errLen = 0) {
        u = UserRec();
        bool inBlock = false;
        while (true) {
            if (!pending_) {
                if (!fgets(line_, sizeof(line_), f_)) return inBlock;
                ++lineNo_;
            }
            pending_ = false;
            char* l = trim(line_);
            if (!*l || *l == '#') continue;
            if (*l == '[') {
                if (inBlock) { pending_ = true; return true; }      // next block starts
                char* end = strchr(l, ']');
                if (!end) { note(problems, err, errLen, "bad [handle] line"); continue; }
                *end = '\0';
                strncpy(u.handle, trim(l + 1), BBS_USER_MAX);
                inBlock = true;
                continue;
            }
            if (!inBlock) { note(problems, err, errLen, "key outside a [handle] block"); continue; }
            char* eq = strchr(l, '=');
            if (!eq) { note(problems, err, errLen, "expected key = value"); continue; }
            *eq = '\0';
            char* key = trim(l);
            char* val = trim(eq + 1);
            assign(u, key, val, problems, err, errLen);
        }
    }

    int lineNo() const { return lineNo_; }

private:
    void note(int* problems, char* err, size_t errLen, const char* what) {
        if (!problems) return;
        if (!*problems && err && errLen) snprintf(err, errLen, "users.txt line %d: %s", lineNo_, what);
        ++*problems;
    }

    void assign(UserRec& u, const char* key, const char* val, int* problems, char* err, size_t errLen) {
        for (uint8_t i = 0; i < kUserFieldCount; ++i) {
            if (!strcmp(key, kUserFields[i].key)) {
                copyUnescaped(users::fieldPtr(u, kUserFields[i]), kUserFields[i].size, val);
                return;
            }
        }
        if (!strcmp(key, "pass"))             { strncpy(u.pass, val, sizeof(u.pass) - 1); }
        else if (!strcmp(key, "created"))     { u.created    = static_cast<uint32_t>(strtoul(val, nullptr, 10)); }
        else if (!strcmp(key, "last_call"))   { u.lastCall   = static_cast<uint32_t>(strtoul(val, nullptr, 10)); }
        else if (!strcmp(key, "calls"))       { u.calls      = static_cast<uint16_t>(strtoul(val, nullptr, 10)); }
        else if (!strcmp(key, "day"))         { u.dayKey     = static_cast<uint32_t>(strtoul(val, nullptr, 10)); }
        else if (!strcmp(key, "day_minutes")) { u.dayMinutes = static_cast<uint16_t>(strtoul(val, nullptr, 10)); }
        else if (!strcmp(key, "locked"))      { u.locked     = ieq(val, "yes") || !strcmp(val, "1"); }
        else note(problems, err, errLen, "unknown key");
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
    fprintf(f, "pass = %s\ncreated = %u\nlast_call = %u\ncalls = %u\nday = %u\nday_minutes = %u\nlocked = %s\n\n",
            u.pass, static_cast<unsigned>(u.created), static_cast<unsigned>(u.lastCall),
            static_cast<unsigned>(u.calls), static_cast<unsigned>(u.dayKey),
            static_cast<unsigned>(u.dayMinutes), u.locked ? "yes" : "no");
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
    if (in) {
        static UserRec u;
        Reader r(in);
        while (r.next(u)) {
            if (replaceHandle && ieq(u.handle, replaceHandle)) {
                found = true;
                if (replacement) writeRecord(out, *replacement);
                continue;
            }
            writeRecord(out, u);
        }
        fclose(in);
    }
    if (append) writeRecord(out, *append);
    bool ok = !ferror(out);
    ok = (fclose(out) == 0) && ok;
    if (replaceHandle && !found) { remove(tmp); return users::Result::NotFound; }
    if (!ok || rename(tmp, live) != 0) {
        remove(live);                                // some filesystems will not rename over a file
        if (!ok || rename(tmp, live) != 0) { remove(tmp); return users::Result::IoError; }
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
    return !ieq(h, "SYSOP") && !isGuestName(h);
}

// isGuestName: GUEST, or GUEST followed by digits (optionally after one
// of space - _ .), which is how guest sessions are named
bool isGuestName(const char* h) {
    static const char kGuest[] = "GUEST";
    for (size_t i = 0; i < 5; ++i) {
        if (toupper(static_cast<unsigned char>(h[i])) != kGuest[i]) return false;
    }
    const char* p = h + 5;
    if (!*p) return true;
    if (*p == ' ' || *p == '-' || *p == '_' || *p == '.') ++p;
    if (!*p) return false;
    for (; *p; ++p) if (!isdigit(static_cast<unsigned char>(*p))) return false;
    return true;
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

bool find(const char* handle, UserRec& out) {
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = fopen(p, "r");
    if (!f) return false;
    Reader r(f);
    bool found = false;
    while (r.next(out)) {
        if (ieq(out.handle, handle)) { found = true; break; }
    }
    fclose(f);
    return found;
}

uint8_t count() {
    char p[96];
    path(p, sizeof(p), "");
    FILE* f = fopen(p, "r");
    if (!f) return 0;
    static UserRec u;
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
    static UserRec u;
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
    static UserRec probe;
    if (find(u.handle, probe)) return Result::Exists;
    if (count() >= syscfg::get().maxUsers) return Result::Full;
    return rewrite(nullptr, nullptr, &u);
}

Result update(const char* originalHandle, const UserRec& u) {
    if (!ieq(originalHandle, u.handle)) {                  // renamed: the new name must be free
        static UserRec probe;
        if (find(u.handle, probe)) return Result::Exists;
    }
    return rewrite(originalHandle, &u, nullptr);
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

int validateFile(const char* p, char* err, size_t errLen) {
    if (err && errLen) err[0] = '\0';
    FILE* f = fopen(p, "r");
    if (!f) { if (err) snprintf(err, errLen, "cannot read users.txt"); return 1; }
    int problems = 0;
    static UserRec u;
    static char seen[BBS_MAX_USERS][BBS_USER_MAX + 1];
    uint16_t n = 0;
    Reader r(f);
    while (r.next(u, &problems, err, errLen)) {
        auto fail = [&](const char* what) {
            if (!problems && err && errLen) snprintf(err, errLen, "users.txt [%.20s]: %s", u.handle, what);
            ++problems;
        };
        if (!validHandle(u.handle))                     fail("invalid handle");
        if (u.pass[0] && (strlen(u.pass) != 81 || u.pass[16] != '$')) fail("pass is not a hash (set passwords on the BBS)");
        for (uint16_t k = 0; k < n && k < BBS_MAX_USERS; ++k) if (ieq(seen[k], u.handle)) fail("duplicate handle");
        if (n < BBS_MAX_USERS) strncpy(seen[n], u.handle, BBS_USER_MAX);
        if (++n > syscfg::get().maxUsers) fail("more accounts than max_users");
    }
    fclose(f);
    return problems;
}

} // namespace users
