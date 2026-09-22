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
 *                  kept. max_users caps the file at 250 accounts; the userdata
 *                  partition has room for far more, but the list indices are
 *                  uint8_t. See BBS_MAX_USERS in config.h.
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
    //
    // id: this account's identity, and the thing everything else should
    // point at. A handle is a display name: it can be changed by its owner
    // or by staff, and before this existed a rename orphaned the caller's
    // mail and their uploads, while a deleted handle could be registered by
    // somebody else who then inherited both. An id is assigned once, is
    // never reused, and outlives the name.
    //
    // 0 means "not assigned yet", which is what every account written
    // before this reads as, and what the first load after upgrading fixes.
    uint32_t id         = 0;
    // retired: the account is finished but its identity is not. It cannot
    // log in and its handle is never handed to anybody else.
    //
    // Deleting the block outright is what made this necessary: the handle
    // went back into circulation, and mail is matched by handle, so the
    // next person to register that name was handed the previous owner's
    // undelivered mail. Keeping the block costs a few hundred bytes on a
    // partition with room for over a thousand accounts, and it is also what
    // makes "the next id is the highest plus one" safe, since a removed
    // block would put its id back in play.
    bool     retired    = false;
    // Staff access, remembered so it does not have to be typed every call.
    //
    // staffAt is when the password was last entered, staffIp is the address
    // it was entered from, and staffLevel is what it bought. Inside the
    // window, a caller logging in from the same address gets that level
    // back without typing it again.
    //
    // Bound to the address on purpose. Account passwords cross this board in
    // the clear on every single login, so remembering staff access against
    // the account alone would turn a sniffed account password into a week of
    // staff access. Tied to an address, a captured password is worth nothing
    // unless the attacker is also on the network it was captured from.
    //
    // Never the sysop. That one is typed every time: it can change every
    // password on the board, read every account and rewrite the config, and
    // it belongs to one person on one machine, so the cost of typing it is
    // small and the cost of not having to is not.
    //
    // Clearing staffLevel in USER EDIT revokes it at the next login. It does
    // not reach into a session that is already elevated; a sysop who needs
    // somebody out now has KICK.
    uint32_t staffAt    = 0;           // epoch, 0 = never
    uint8_t  staffLevel = 0;           // Access value it was confirmed at
    char     staffIp[16] = {};         // the address it was confirmed from
    char     pass[82]   = {};          // "salt16hex$sha256hex", empty = no password
    uint32_t created    = 0;           // epoch, 0 = clock not set
    uint32_t lastCall   = 0;           // epoch of the last login
    uint16_t calls      = 0;
    uint32_t dayKey     = 0;           // clk::dayKey of dayMinutes
    uint16_t dayMinutes = 0;           // minutes used that day
    // Where this caller lands after login.
    //
    // LAND_DEFAULT is 0, which is what every account written before this
    // existed parses as, so nothing needs migrating and an account that has
    // never been asked simply follows whatever the sysop set.
    uint8_t  land       = 0;
    bool     locked     = false;       // sysop lock
    uint8_t  level      = 0;           // staff rank, an Access value: 0 user,
                                       // 1 co-sysop 2, 2 co-sysop 1, 3 sysop.
                                       // Set when that password is entered;
                                       // staff may only change their own level
                                       // and below.
};

// Where a caller is put when they log in. The board's own default fills in
// for LAND_DEFAULT, and a landing whose command does not exist on this board
// falls back to the main prompt rather than failing, which is what lets
// FORUMS be offered before the message boards are written.
//
// "Forums" rather than "bulletin": on this system "board" already means the
// BBS itself, and "messages" would blur into MAIL. Forums is the word a
// caller who has never used a BBS already knows.
//
// "bulletin" is retired entirely (Rob, 2026-09-22). It had been used for
// the message boards, for the screen that plays at login, and was about to
// be used for the /i information pages as well. The login screen is
// screens/motd.* and the information pages are NEWS. The only survivals
// are back-compat for data already written: the hidden FORUMS alias and
// landFromKey accepting land=bulletin.
enum : uint8_t { LAND_DEFAULT = 0, LAND_MAIN = 1, LAND_CHAT = 2, LAND_FORUMS = 3 };

namespace users {
const char* landKey(uint8_t v);          // "default" | "main" | "chat" | "forums"
uint8_t     landFromKey(const char* s);
const char* landVerb(uint8_t v);         // the command that puts them there, or null

// maxId: the highest id in users.txt, 0 on an empty or unreadable file.
// The next id is this plus one, derived rather than stored so there is no
// counter to keep in step, lose in a restore, or have disagree with the
// file. That is only safe because accounts are never removed: delete a
// block and its id becomes available again on the next pass.
uint32_t maxId();

// assignIds: give an id to every account that has none, once, at startup.
// Returns how many it handed out. Does nothing and reports 0 when every
// account already has one, which is every boot after the first.
uint8_t assignIds();
}

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

// retire: stop the account working, keep its identity and reserve its
// handle for ever. This is what USER DEL does.
Result retire(const char* handle);

// purge: blank a person's details, keep the identity and the reservation.
Result purge(const char* handle);

// remove: delete the block outright. Only for an account nobody has ever
// used; anything else must retire, or a handle goes back into circulation
// and its id becomes available again.
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
