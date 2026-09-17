/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/users.h
 * Module:       Core / user accounts
 *
 * Purpose:      User accounts in <fs>/users.txt (storage partition, travels
 *                  in the backup zip). One block per user:
 *
 *                    [Handle]
 *                    name = Rob
 *                    email = rob@example.com
 *                    ...
 *
 *                  Text fields are described by kUserFields: adding a field is one member
 *                  in UserRec plus one row in the table (users.cpp); forms, INFO and the
 *                  file format pick it up. System fields (password hash, counters, lock)
 *                  are handled explicitly.
 *
 *                  Every change rewrites the file through a temp file and a rename, so a
 *                  power cut leaves either the old or the new file. Unknown keys are not
 *                  kept. Onboard storage allows about 100 accounts (max_users).
 *
 * Design:       One record is read at a time, so memory use does not grow with the
 *               number of accounts. Every change rewrites the file through a temp file
 *               and a rename, so a power cut leaves the old file or the new one.
 *
 * Interfaces:   users::find, count, at, range, add, update, remove, setPassword,
 *               checkPassword, validateFile, validHandle, validEmail
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     USERS.md
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
    uint8_t  level      = 0;           // staff rank, an Access value: 0 user,
                                       // 1 co-sysop 2, 2 co-sysop 1, 3 sysop.
                                       // Set when that password is entered;
                                       // staff may only change their own level
                                       // and below.
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

// lookup: like find, but says whether users.txt could be read at all, so a
// caller never treats a filesystem problem as "no such account"
enum class Lookup : uint8_t { Found, Missing, Error };
Lookup lookup(const char* handle, UserRec& out);

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

// Issues: what a parse found. problems reject the file, warnings do not
// (an unknown key is dropped the next time the file is written). Both
// messages name the line number.
struct Issues {
    uint8_t maxUsers = 0;              // 0 = the live max_users
    int    problems = 0;
    char*  err      = nullptr;
    size_t errLen   = 0;
    int    warnings = 0;
    char*  warn     = nullptr;
    size_t warnLen  = 0;
};

// validateFile: a users.txt from an upload parses cleanly (handles valid
// and unique, passwords well formed). Problems count, first one in err.
int validateFile(const char* path, Issues& issues);

} // namespace users
