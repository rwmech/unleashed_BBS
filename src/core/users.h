/*
 * File:        src/core/users.h
 * Description: User accounts in <fs>/users.txt (storage partition, travels
 *              in the backup zip). One block per user:
 *
 *                [Handle]
 *                name = Rob
 *                email = rob@example.com
 *                ...
 *
 *   Text fields are described by kUserFields: adding a field is one member
 *   in UserRec plus one row in the table (users.cpp); forms, INFO and the
 *   file format pick it up. System fields (password hash, counters, lock)
 *   are handled explicitly.
 *
 *   Every change rewrites the file through a temp file and a rename, so a
 *   power cut leaves either the old or the new file. Unknown keys are not
 *   kept. Onboard storage allows about 100 accounts (max_users).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc stdio)
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include "../config.h"

struct UserRec {
    // text fields (see kUserFields)
    char     handle[BBS_USER_MAX + 1] = {};
    char     name[33]                 = {};
    char     email[65]                = {};
    char     address[65]              = {};
    char     phone[21]                = {};
    char     profile[BBS_PROFILE_MAX + 1] = {};
    // system fields
    char     pass[82]   = {};          // "salt16hex$sha256hex", empty = no password
    uint32_t created    = 0;           // epoch, 0 = clock not set
    uint32_t lastCall   = 0;           // epoch of the last login
    uint16_t calls      = 0;
    uint32_t dayKey     = 0;           // clk::dayKey of dayMinutes
    uint16_t dayMinutes = 0;           // minutes used that day
    bool     locked     = false;       // sysop lock
};

enum UserFieldFlag : uint8_t {
    UF_NONE     = 0,
    UF_REQUIRED = 1,    // must not be empty
    UF_PRIVATE  = 2,    // shown only to the owner and staff with USERS
    UF_TEXTAREA = 4,    // multi-row box in forms
    UF_EMAIL    = 8,    // validated as an email address
};

struct UserField {
    const char* key;      // key in users.txt
    const char* label;    // form label, 9 characters max
    uint16_t    offset;   // offsetof(UserRec, field)
    uint8_t     size;     // buffer size including the terminator
    uint8_t     flags;    // UserFieldFlag
};

// Editable text fields after the handle, in form order
extern const UserField kUserFields[];
extern const uint8_t   kUserFieldCount;

namespace users {

// validHandle: letters, digits, space - _ . ; starts alphanumeric; not SYSOP
bool validHandle(const char* h);

// validEmail: something@something.something, no spaces
bool validEmail(const char* e);

// field access through the table
char*       fieldPtr(UserRec& u, const UserField& f);
const char* fieldPtr(const UserRec& u, const UserField& f);

// find: case-insensitive handle lookup
bool find(const char* handle, UserRec& out);

// count: accounts in the file
uint8_t count();

// at: index-th account in file order (for listings)
bool at(uint8_t index, UserRec& out);

// range: accounts start..start+n-1 in one pass; fn gets (ctx, index, record)
using RangeFn = void (*)(void* ctx, uint8_t index, const UserRec& u);
uint8_t range(uint8_t start, uint8_t n, RangeFn fn, void* ctx);

enum class Result : uint8_t { Ok, Exists, Full, NotFound, IoError };

// add: new account (fails when the handle exists or max_users is reached)
Result add(const UserRec& u);

// update: replace the account named originalHandle (handle may change)
Result update(const char* originalHandle, const UserRec& u);

// remove: delete an account
Result remove(const char* handle);

// setPassword / checkPassword: salted SHA-256, BBS_PASS_ROUNDS rounds
void setPassword(UserRec& u, const char* password);
bool checkPassword(const UserRec& u, const char* password);

// validateFile: a users.txt from an upload parses cleanly (handles valid
// and unique, passwords well formed). Problems count, first one in err.
int validateFile(const char* path, char* err, size_t errLen);

} // namespace users
