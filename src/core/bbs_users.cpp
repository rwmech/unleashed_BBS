/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/bbs_users.cpp
 * Module:       Core / accounts in the shell
 *
 * Purpose:      Accounts in the shell: the fill-in forms (signup, PROFILE,
 *                  PASSWORD, staff add/edit), INFO, USER ADD|EDIT|DEL, and the
 *                  USERS manager screen (cursor-driven list on ANSI/PETSCII, a
 *                  paged list on plain ASCII). Guide: USERS.md.
 *
 * Libraries:    none (libc)
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

#include "bbs.h"
#include "bbs_util.h"
#include "fx.h"
#include "clock.h"
#include "sysconfig.h"
#include "users.h"
#include "../platform/platform.h"

#include <cstring>
#include <cstdio>

using namespace bbsu;

namespace {

void wipe(char* s, size_t n) { memset(s, 0, n); }

void say(Term& t, Timeline& tl, Color c, const char* s) {
    t.color(tl, c);
    t.text(tl, s);
}

constexpr uint8_t kListTop = 4;     // user manager: first list row

const char* kLevelNames[] = { "User", "Co2", "Co1", "Sysop" };

// levelText / levelValue: the Level form field
const char* levelText(uint8_t level) { return kLevelNames[level < 4 ? level : 0]; }

uint8_t levelValue(const char* text) {
    for (uint8_t i = 0; i < 4; ++i) if (ieq(text, kLevelNames[i])) return i;
    return 0;
}

// levelChoices: the ranks a staff member may hand out, their own and below
const char* levelChoices(Access level) {
    switch (level) {
        case Access::Sysop:    return "User|Co2|Co1|Sysop";
        case Access::CoSysop1: return "User|Co2|Co1";
        case Access::CoSysop2: return "User|Co2";
        default:               return "User";
    }
}

// mayManage: staff may only touch accounts at their own rank and below
bool mayManage(const Session& s, const UserRec& u) {
    return u.level <= static_cast<uint8_t>(s.level);
}

// copyFields: move the editable text fields from the form copy onto a
// record just read from the file, so nothing else is written back stale
void copyFields(const UserRec& from, UserRec& to) {
    for (uint8_t i = 0; i < kUserFieldCount; ++i) {
        const UserField& f = kUserFields[i];
        strncpy(users::fieldPtr(to, f), users::fieldPtr(from, f), f.size - 1);
        users::fieldPtr(to, f)[f.size - 1] = 0;
    }
}

} // namespace

// ===========================================================================
// Forms
// ===========================================================================

void Bbs::addField(Session& s, uint8_t& n, const char* label, char* buf, uint8_t cap, uint8_t flags,
                   const char* choices) {
    if (n >= Form::kMaxFields) return;
    FormField& f = s.fields[n++];
    f.label   = label;
    f.buf     = buf;
    f.cap     = cap;
    f.flags   = flags;
    f.choices = choices;
}

// addUserFields: the kUserFields table as form fields; returns the index of the first
uint8_t Bbs::addUserFields(Session& s, uint8_t n) {
    uint8_t first = n;
    for (uint8_t i = 0; i < kUserFieldCount; ++i) {
        const UserField& uf = kUserFields[i];
        uint8_t flags = FF_NONE;
        if (uf.flags & UF_REQUIRED) flags |= FF_REQUIRED;
        if (uf.flags & UF_TEXTAREA) flags |= FF_TEXTAREA;
        addField(s, n, uf.label, users::fieldPtr(s.edit, uf), static_cast<uint8_t>(uf.size - 1), flags);
    }
    (void)first;
    return n;
}

// ---------------------------------------------------------------------------
// startForm: lay out the fields for one kind of form and draw it.
// UserEdit expects s.edit and s.origHandle to be loaded.
// ---------------------------------------------------------------------------
void Bbs::startForm(Session& s, FormKind kind, uint32_t now) {
    (void)now;
    uint8_t n = 0;
    const char* title = "";
    s.formKind = kind;
    wipe(s.pwA, sizeof(s.pwA));
    wipe(s.pwB, sizeof(s.pwB));
    wipe(s.pwC, sizeof(s.pwC));

    switch (kind) {
        case FormKind::Signup: {
            char handle[BBS_USER_MAX + 1];
            strncpy(handle, s.user, sizeof(handle));
            s.edit = UserRec();
            strncpy(s.edit.handle, handle, BBS_USER_MAX);
            title = "NEW ACCOUNT";
            addField(s, n, "Handle", s.edit.handle, BBS_USER_MAX, FF_READONLY);
            addField(s, n, "Password", s.pwA, BBS_PASS_MAX, FF_MASK | FF_REQUIRED);
            addField(s, n, "Again", s.pwB, BBS_PASS_MAX, FF_MASK | FF_REQUIRED);
            n = addUserFields(s, n);
            break;
        }
        case FormKind::Profile:
            title = "YOUR PROFILE";
            addField(s, n, "Handle", s.edit.handle, BBS_USER_MAX, FF_READONLY);
            n = addUserFields(s, n);
            break;
        case FormKind::Password:
            title = "CHANGE PASSWORD";
            addField(s, n, "Current", s.pwA, BBS_PASS_MAX, FF_MASK | FF_REQUIRED);
            addField(s, n, "New", s.pwB, BBS_PASS_MAX, FF_MASK | FF_REQUIRED);
            addField(s, n, "Again", s.pwC, BBS_PASS_MAX, FF_MASK | FF_REQUIRED);
            break;
        case FormKind::UserAdd:
            title = "ADD ACCOUNT";
            s.edit = UserRec();
            strcpy(s.yesno, "N");
            strcpy(s.levelBuf, "User");
            addField(s, n, "Handle", s.edit.handle, BBS_USER_MAX, FF_REQUIRED);
            addField(s, n, "Password", s.pwA, BBS_PASS_MAX, FF_MASK | FF_REQUIRED);
            n = addUserFields(s, n);
            addField(s, n, "Level", s.levelBuf, 5, FF_CYCLE, levelChoices(s.level));
            addField(s, n, "Locked", s.yesno, 1, FF_YESNO);
            break;
        case FormKind::UserEdit:
            title = "EDIT ACCOUNT";
            strcpy(s.yesno, s.edit.locked ? "Y" : "N");
            snprintf(s.levelBuf, sizeof(s.levelBuf), "%s", levelText(s.edit.level));
            addField(s, n, "Handle", s.edit.handle, BBS_USER_MAX, FF_REQUIRED);
            addField(s, n, "New pass", s.pwA, BBS_PASS_MAX, FF_MASK);
            n = addUserFields(s, n);
            addField(s, n, "Level", s.levelBuf, 5, FF_CYCLE, levelChoices(s.level));
            addField(s, n, "Locked", s.yesno, 1, FF_YESNO);
            break;
        default:
            return;
    }
    s.ed = LineEditor();
    s.st = SState::Form;
    s.lastInput = plat::millis();
    s.form.begin(title, s.fields, n, s.term, s.tl);
}

// checkUserFields: required and email rules from the field table
bool Bbs::checkUserFields(Session& s, uint8_t firstField) {
    char msg[48];
    for (uint8_t i = 0; i < kUserFieldCount; ++i) {
        const UserField& uf = kUserFields[i];
        const char* v = users::fieldPtr(s.edit, uf);
        if ((uf.flags & UF_REQUIRED) && !*v) {
            snprintf(msg, sizeof(msg), "%s is required", uf.label);
            s.form.fail(static_cast<uint8_t>(firstField + i), msg, s.term, s.tl);
            return false;
        }
        if ((uf.flags & UF_EMAIL) && *v && !users::validEmail(v)) {
            s.form.fail(static_cast<uint8_t>(firstField + i), "That email does not look right", s.term, s.tl);
            return false;
        }
    }
    return true;
}

// formDone: leave the form with a result line, back to the prompt or list
void Bbs::formDone(Session& s, Color c, const char* msg) {
    s.form.after(s.term, s.tl);
    s.formKind = FormKind::None;
    wipe(s.pwA, sizeof(s.pwA));
    wipe(s.pwB, sizeof(s.pwB));
    wipe(s.pwC, sizeof(s.pwC));
    if (s.backToUsers) {
        s.backToUsers = false;
        ulOpen(s);
        s.term.gotoXY(s.tl, 2, static_cast<uint8_t>(kListTop + ulRows(s) + 3));
        say(s.term, s.tl, c, msg);
        return;
    }
    s.term.nl(s.tl);
    say(s.term, s.tl, c, msg);
    prompt(s);
}

// ---------------------------------------------------------------------------
// formSave: validate, write, report
// ---------------------------------------------------------------------------
void Bbs::formSave(Session& s, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char msg[64];

    switch (s.formKind) {
        case FormKind::Config: {
            char err[80] = "";
            if (!configSave(s, err, sizeof(err))) return;          // the form said why
            configRelease(s);
            formDone(s, Color::LightGreen, err);
            return;
        }

        // A sub-page saves its one row and goes back to the page it came
        // from. The editing guard is deliberately NOT released: the sysop is
        // still in CONFIG, just one level up.
        case FormKind::ConfigArea: {
            char err[80] = "";
            if (!configSubSave(s, err, sizeof(err))) return;       // the form said why
            configSubBack(s, Color::LightGreen, err, now);
            return;
        }
        case FormKind::Signup: {
            if (!users::validHandle(s.edit.handle) || ieq(s.edit.handle, "SYSOP") ||
                handleOnline(s, s.edit.handle)) {     // the read-only handle cannot be fixed here
                s.form.after(t, tl);
                s.formKind = FormKind::None;
                wipe(s.pwA, sizeof(s.pwA));
                wipe(s.pwB, sizeof(s.pwB));
                t.nl(tl);
                say(t, tl, Color::LightRed, "That handle cannot be used. Pick another.");
                s.user[0] = '\0';
                askName(s);
                return;
            }
            if (strlen(s.pwA) < BBS_PASS_MIN) { s.form.fail(1, "Password needs 4 or more characters", t, tl); return; }
            if (strcmp(s.pwA, s.pwB))          { s.form.fail(2, "The passwords do not match", t, tl); return; }
            if (!checkUserFields(s, 3)) return;
            users::setPassword(s.edit, s.pwA);
            s.edit.created = clk::epoch();
            users::Result r = users::add(s.edit);
            if (r == users::Result::Exists) { s.form.fail(0, "That handle was just taken", t, tl); return; }
            if (r == users::Result::Full)   { s.form.fail(0, "Sign-ups are closed: BBS is full", t, tl); return; }
            if (r != users::Result::Ok)     { s.form.fail(0, "Could not save, try again", t, tl); return; }
            plat::log("bbs: node %u new account '%s'", s.id, s.edit.handle);
            s.form.after(t, tl);
            s.formKind = FormKind::None;
            wipe(s.pwA, sizeof(s.pwA));
            wipe(s.pwB, sizeof(s.pwB));
            t.nl(tl);
            t.color(tl, Color::Grey);
            t.text(tl, "Creating account ");
            t.color(tl, Color::LightGreen);
            fx::progressBar(t, tl, 16, 900);
            t.nl(tl);
            t.color(tl, Color::Yellow);
            fx::scramble(t, tl, "WELCOME ABOARD", 10, 55);
            t.nl(tl);
            strncpy(s.user, s.edit.handle, BBS_USER_MAX);
            s.newAccount = true;          // first call: the short rules, not the bulletin
            completeLogin(s, now);
            return;
        }

        case FormKind::Profile: {
            if (!checkUserFields(s, 1)) return;
            static UserRec cur;                      // re-read: a lock or password set
            if (!users::find(s.user, cur)) {         // while this form was open must stand
                formDone(s, Color::LightRed, "Your account is gone.");
                return;
            }
            copyFields(s.edit, cur);
            users::Result r = users::update(s.user, cur);
            formDone(s, r == users::Result::Ok ? Color::LightGreen : Color::LightRed,
                     r == users::Result::Ok ? "Profile saved." : "Could not save the profile.");
            return;
        }

        case FormKind::Password: {
            static UserRec cur;
            if (!users::find(s.user, cur)) { formDone(s, Color::LightRed, "No account on this line."); return; }
            if (!users::checkPassword(cur, s.pwA)) {
                bool lockedNow = logins_.fail(s.user, now);
                if (lockedNow) {
                    s.form.after(t, tl);
                    hangup(s, "Too many wrong passwords.", now);
                    return;
                }
                s.form.fail(0, "The current password is wrong", t, tl);
                return;
            }
            if (strlen(s.pwB) < BBS_PASS_MIN) { s.form.fail(1, "Password needs 4 or more characters", t, tl); return; }
            if (strcmp(s.pwB, s.pwC))          { s.form.fail(2, "The passwords do not match", t, tl); return; }
            users::setPassword(cur, s.pwB);
            users::Result r = users::update(s.user, cur);
            formDone(s, r == users::Result::Ok ? Color::LightGreen : Color::LightRed,
                     r == users::Result::Ok ? "Password changed." : "Could not save the password.");
            return;
        }

        case FormKind::UserAdd:
        case FormKind::UserEdit: {
            bool adding = s.formKind == FormKind::UserAdd;
            if (!users::validHandle(s.edit.handle) || ieq(s.edit.handle, "SYSOP")) {
                s.form.fail(0, "Handle: letters, digits, space - _ .", t, tl);
                return;
            }
            if (adding && strlen(s.pwA) < BBS_PASS_MIN) { s.form.fail(1, "Password needs 4 or more characters", t, tl); return; }
            if (!adding && *s.pwA && strlen(s.pwA) < BBS_PASS_MIN) { s.form.fail(1, "Password needs 4 or more characters", t, tl); return; }
            if (!checkUserFields(s, 2)) return;
            uint8_t wantLevel = levelValue(s.levelBuf);
            if (wantLevel > static_cast<uint8_t>(s.level)) {
                s.form.fail(0, "Level above your own", t, tl);
                return;
            }
            bool renamed = adding || !ieq(s.edit.handle, s.origHandle);
            if (renamed && handleOnline(s, s.edit.handle)) {
                s.form.fail(0, "That handle is online right now", t, tl);
                return;
            }
            static UserRec cur;
            if (!adding) {                           // write onto the record as it is now
                if (!users::find(s.origHandle, cur)) { formDone(s, Color::LightRed, "That account no longer exists."); return; }
                if (!mayManage(s, cur)) { formDone(s, Color::LightRed, "That account is above your level."); return; }
                copyFields(s.edit, cur);
                strncpy(cur.handle, s.edit.handle, BBS_USER_MAX);
            } else {
                cur = s.edit;
                cur.created = clk::epoch();
            }
            cur.locked = s.yesno[0] == 'Y';
            cur.level  = wantLevel;
            if (*s.pwA) users::setPassword(cur, s.pwA);
            s.edit = cur;
            users::Result r = adding ? users::add(cur) : users::update(s.origHandle, cur);
            if (r == users::Result::Exists) { s.form.fail(0, "That handle is taken", t, tl); return; }
            if (r == users::Result::Full)   { s.form.fail(0, "max_users reached", t, tl); return; }
            if (r == users::Result::NotFound) { formDone(s, Color::LightRed, "That account no longer exists."); return; }
            if (r != users::Result::Ok)     { s.form.fail(0, "Could not save, try again", t, tl); return; }
            if (!adding) {                                       // renamed: callers online keep their session
                for (Session* o : all_) {
                    if (o->st != SState::Free && ieq(o->user, s.origHandle)) strncpy(o->user, s.edit.handle, BBS_USER_MAX);
                }
            }
            plat::log("bbs: %s %s account '%s'", s.user, adding ? "added" : "saved", s.edit.handle);
            snprintf(msg, sizeof(msg), adding ? "Account %s added." : "Account %s saved.", s.edit.handle);
            formDone(s, Color::LightGreen, msg);
            return;
        }

        default:
            formDone(s, Color::Grey, "");
            return;
    }
}

// ---------------------------------------------------------------------------
// formOpen: a button on the form was pressed. CONFIG is the only form with
// buttons; any other kind ignores it rather than guessing.
// ---------------------------------------------------------------------------
void Bbs::formOpen(Session& s, uint8_t field, uint32_t now) {
    if (s.formKind == FormKind::Config) configSubOpen(s, field, now);
}

void Bbs::formCancel(Session& s, uint32_t now) {
    // Cancelling a sub-page is not leaving CONFIG: the sysop lands back on
    // the page the button was on and keeps the editing guard. Releasing it
    // here would let a second staff session walk in under the one still
    // holding the parent page.
    if (s.formKind == FormKind::ConfigArea) {
        configSubBack(s, Color::Grey, "Nothing changed", now);
        return;
    }
    configRelease(s);
    (void)now;
    if (s.formKind == FormKind::Signup) {
        s.form.after(s.term, s.tl);
        s.formKind = FormKind::None;
        wipe(s.pwA, sizeof(s.pwA));
        wipe(s.pwB, sizeof(s.pwB));
        s.term.nl(s.tl);
        say(s.term, s.tl, Color::Grey, "Sign-up cancelled.");
        askName(s);
        return;
    }
    formDone(s, Color::Grey, "Cancelled, nothing changed.");
}

// ===========================================================================
// Caller commands
// ===========================================================================

void Bbs::cmdProfile(Session& s, uint32_t now) {
    if (s.role == Role::Busy || !users::find(s.user, s.edit)) {
        say(s.term, s.tl, Color::LightRed, "No account on this line.");
        prompt(s);
        return;
    }
    s.backToUsers = false;
    startForm(s, FormKind::Profile, now);
}

void Bbs::cmdPassword(Session& s, uint32_t now) {
    static UserRec probe;
    if (s.role == Role::Busy || !users::find(s.user, probe)) {
        say(s.term, s.tl, Color::LightRed, "No account on this line.");
        prompt(s);
        return;
    }
    s.backToUsers = false;
    startForm(s, FormKind::Password, now);
}

// ---------------------------------------------------------------------------
// cmdInfo: INFO [handle]. Private fields only for the owner or USERS staff.
// ---------------------------------------------------------------------------
void Bbs::cmdInfo(Session& s, const char* arg) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    static UserRec u;
    const char* who = *arg ? arg : s.user;
    if (!*arg && s.guest) {
        say(t, tl, Color::Yellow, "Guests have no account.");
        prompt(s);
        return;
    }
    if (!users::find(who, u)) {
        say(t, tl, Color::LightRed, "No account by that name.");
        prompt(s);
        return;
    }
    bool privateOk = ieq(u.handle, s.user) || can(s, PERM_USERS);
    char buf[64];
    char when[24];

    t.color(tl, Color::Yellow);
    fx::typewriter(t, tl, u.handle, 20);
    t.nl(tl);
    t.color(tl, Color::Cyan);
    fx::rule(t, tl, 39);
    t.nl(tl);
    for (uint8_t i = 0; i < kUserFieldCount; ++i) {
        const UserField& uf = kUserFields[i];
        if ((uf.flags & UF_TEXTAREA) || ((uf.flags & UF_PRIVATE) && !privateOk)) continue;
        const char* v = users::fieldPtr(u, uf);
        if (!*v) continue;
        snprintf(buf, sizeof(buf), "%-9s", uf.label);
        say(t, tl, Color::LightBlue, buf);
        t.color(tl, Color::White);
        t.textN(tl, v, 29);
        t.nl(tl);
    }
    clk::fmtEpoch(when, sizeof(when), "%d %b %Y", u.created);
    snprintf(buf, sizeof(buf), "%-9s%s", "Member", when);
    say(t, tl, Color::Grey, buf);
    t.nl(tl);
    clk::fmtEpoch(when, sizeof(when), "%d %b %Y %H:%M", u.lastCall);
    snprintf(buf, sizeof(buf), "%-9s%s, %u calls", "Last", when, u.calls);
    say(t, tl, Color::Grey, buf);
    t.nl(tl);
    if (u.level) {
        snprintf(buf, sizeof(buf), "%-9s%s", "Staff", syscfg::levelName(static_cast<Access>(u.level)));
        say(t, tl, Color::Yellow, buf);
        t.nl(tl);
    }
    if (can(s, PERM_USERS) && u.locked) {
        say(t, tl, Color::LightRed, "Locked by the sysop");
        t.nl(tl);
    }
    for (uint8_t i = 0; i < kUserFieldCount; ++i) {
        const UserField& uf = kUserFields[i];
        if (!(uf.flags & UF_TEXTAREA)) continue;
        const char* v = users::fieldPtr(u, uf);
        size_t len = strlen(v);
        for (size_t off = 0; off < len; off += 37) {
            t.color(tl, Color::Grey);
            t.text(tl, " ");
            t.textN(tl, v + off, 37);
            t.nl(tl);
        }
    }
    prompt(s);
}

// ---------------------------------------------------------------------------
// cmdUser: USER ADD | USER EDIT handle | USER DEL handle (works everywhere,
// including plain ASCII terminals)
// ---------------------------------------------------------------------------
void Bbs::cmdUser(Session& s, const char* arg, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    char sub[8] = {};
    size_t i = 0;
    while (arg[i] && arg[i] != ' ' && i < sizeof(sub) - 1) { sub[i] = arg[i]; ++i; }
    const char* who = arg + i;
    while (*who == ' ') ++who;
    s.backToUsers = false;

    if (ieq(sub, "ADD")) { startForm(s, FormKind::UserAdd, now); return; }

    if ((ieq(sub, "EDIT") || ieq(sub, "DEL")) && *who) {
        if (!users::find(who, s.edit)) {
            say(t, tl, Color::LightRed, "No account by that name.");
            prompt(s);
            return;
        }
        if (!mayManage(s, s.edit)) {
            say(t, tl, Color::LightRed, "That account is above your level.");
            prompt(s);
            return;
        }
        strncpy(s.origHandle, s.edit.handle, BBS_USER_MAX);
        if (ieq(sub, "EDIT")) { startForm(s, FormKind::UserEdit, now); return; }
        if (ieq(s.origHandle, s.user)) {
            say(t, tl, Color::LightRed, "You cannot delete your own account.");
            prompt(s);
            return;
        }
        s.confirm = ConfirmKind::DeleteUser;
        s.st = SState::Confirm;
        t.color(tl, Color::Yellow);
        t.text(tl, "Delete ");
        t.color(tl, Color::White);
        t.text(tl, s.origHandle);
        t.color(tl, Color::Yellow);
        t.text(tl, " (y/N)? ");
        return;
    }
    say(t, tl, Color::LightRed, "USER ADD | USER EDIT h | USER DEL h");
    prompt(s);
}

// ===========================================================================
// USERS manager
// ===========================================================================

void Bbs::cmdUsers(Session& s, uint32_t now) {
    (void)now;
    if (s.term.isAnsi() || s.term.isPet()) {
        s.ulSel = 0;
        s.ulTop = 0;
        ulOpen(s);
        return;
    }
    startList(s, ListKind::Users);                     // plain ASCII: paged list
}

uint8_t Bbs::ulRows(const Session& s) const {
    uint8_t rows = s.term.rows();
    uint8_t n = static_cast<uint8_t>(rows > kListTop + 5 ? rows - kListTop - 4 : 4);
    return n > BBS_USERLIST_ROWS ? BBS_USERLIST_ROWS : n;
}

namespace {

void drawUserLine(Term& t, Timeline& tl, const UserRec& u, bool selected) {
    char buf[48];
    char mark = u.level >= static_cast<uint8_t>(Access::Sysop) ? ']' : (u.level ? '>' : ' ');
    snprintf(buf, sizeof(buf), "%c%-13.13s %-16.16s %4u %c", mark, u.handle, u.name,
             static_cast<unsigned>(u.calls), u.locked ? 'L' : ' ');
    if (selected) { t.reverse(tl, true); t.color(tl, Color::White); }
    else          t.color(tl, u.locked ? Color::DarkGrey : Color::Grey);
    size_t n = strlen(buf);
    t.text(tl, buf);
    for (; n < 38; ++n) t.ch(tl, ' ');
    if (selected) t.reverse(tl, false);
}

} // namespace

// ---------------------------------------------------------------------------
// ulOpen: full redraw of the manager (sequential, cheap on PETSCII)
// ---------------------------------------------------------------------------
void Bbs::ulOpen(Session& s) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    const SysConfig& cfg = syscfg::get();
    uint8_t h = ulRows(s);

    s.st      = SState::UserList;
    s.ed      = LineEditor();
    s.ulCount = users::count();
    if (s.ulCount && s.ulSel >= s.ulCount) s.ulSel = static_cast<uint8_t>(s.ulCount - 1);
    if (s.ulSel < s.ulTop) s.ulTop = s.ulSel;
    if (s.ulSel >= s.ulTop + h) s.ulTop = static_cast<uint8_t>(s.ulSel - h + 1);

    char buf[48];
    t.reset(tl);
    t.cursor(tl, false);
    t.cls(tl);
    snprintf(buf, sizeof(buf), "%u of %u", s.ulCount, cfg.maxUsers);
    t.color(tl, Color::Yellow);
    fx::scramble(t, tl, "USER MANAGER", 5, 40);
    t.color(tl, Color::Grey);
    char pad[64];
    snprintf(pad, sizeof(pad), "%*.12s", 27, buf);
    t.text(tl, pad);
    t.nl(tl);
    t.color(tl, Color::Cyan);
    fx::rule(t, tl, 39);
    t.nl(tl);
    t.color(tl, Color::LightBlue);
    t.text(tl, " Handle        Name             Calls");
    t.nl(tl);

    struct Ctx { Term* t; Timeline* tl; uint8_t sel; uint8_t drawn; };
    Ctx ctx{ &t, &tl, s.ulSel, 0 };
    users::range(s.ulTop, h, [](void* c, uint8_t index, const UserRec& u) {
        Ctx* x = static_cast<Ctx*>(c);
        drawUserLine(*x->t, *x->tl, u, index == x->sel);
        x->t->nl(*x->tl);
        ++x->drawn;
    }, &ctx);
    if (!s.ulCount) {
        t.color(tl, Color::DarkGrey);
        t.text(tl, " No accounts yet. A adds one.");
        t.nl(tl);
        ++ctx.drawn;
    }
    for (; ctx.drawn < h; ++ctx.drawn) t.nl(tl);

    t.color(tl, Color::Cyan);
    fx::rule(t, tl, 39);
    t.nl(tl);
    t.color(tl, Color::DarkGrey);
    t.text(tl, kMarkKey);
    t.nl(tl);
    t.color(tl, Color::DarkGrey);
    t.text(tl, t.isPet() ? "RETURN edit A add D delete Q quit" : "Enter edit  A add  D delete  Q quit");
}

// ulStatus: one line under the manager list (result, warning)
void Bbs::ulStatus(Session& s, Color c, const char* msg) {
    char pad[44];
    snprintf(pad, sizeof(pad), "%-38.38s", msg);
    s.term.gotoXY(s.tl, 2, static_cast<uint8_t>(kListTop + ulRows(s) + 3));
    say(s.term, s.tl, c, pad);
}

void Bbs::ulDrawRow(Session& s, uint8_t index) {
    if (index < s.ulTop || index >= s.ulTop + ulRows(s)) return;
    static UserRec u;
    if (!users::at(index, u)) return;
    s.term.gotoXY(s.tl, 1, static_cast<uint8_t>(kListTop + index - s.ulTop));
    drawUserLine(s.term, s.tl, u, index == s.ulSel);
}

// ---------------------------------------------------------------------------
// ulKey: move, edit, add, delete, quit
// ---------------------------------------------------------------------------
void Bbs::ulKey(Session& s, int k, uint32_t now) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t h = ulRows(s);

    if (k == KEY_UP || k == KEY_DOWN) {
        if (!s.ulCount) return;
        uint8_t old = s.ulSel;
        if (k == KEY_UP && s.ulSel > 0) --s.ulSel;
        if (k == KEY_DOWN && s.ulSel + 1 < s.ulCount) ++s.ulSel;
        if (old == s.ulSel) return;
        if (s.ulSel < s.ulTop || s.ulSel >= s.ulTop + h) { ulOpen(s); return; }   // scroll
        ulDrawRow(s, old);
        ulDrawRow(s, s.ulSel);
        return;
    }
    if (k == 'q' || k == 'Q' || k == KEY_ESC || k == KEY_BREAK) {
        t.reset(tl);
        t.cursor(tl, true);
        t.gotoXY(tl, 1, static_cast<uint8_t>(kListTop + h + 3));
        prompt(s);
        return;
    }
    if (k == 'a' || k == 'A') {
        s.backToUsers = true;
        t.cursor(tl, true);
        startForm(s, FormKind::UserAdd, now);
        return;
    }
    if (!s.ulCount) return;
    if (k == KEY_ENTER || k == 'e' || k == 'E' || k == 'd' || k == 'D') {
        if (!users::at(s.ulSel, s.edit)) { ulOpen(s); return; }
        if (!mayManage(s, s.edit)) {
            t.cursor(tl, true);
            ulStatus(s, Color::LightRed, "That account is above your level.");
            return;
        }
        strncpy(s.origHandle, s.edit.handle, BBS_USER_MAX);
        t.cursor(tl, true);
        if (k == 'd' || k == 'D') {
            t.gotoXY(tl, 2, static_cast<uint8_t>(kListTop + h + 3));
            if (ieq(s.origHandle, s.user)) {
                say(t, tl, Color::LightRed, "You cannot delete your own account.  ");
                return;
            }
            s.backToUsers = true;
            s.confirm = ConfirmKind::DeleteUser;
            s.st = SState::Confirm;
            say(t, tl, Color::Yellow, "Delete ");
            say(t, tl, Color::White, s.origHandle);
            say(t, tl, Color::Yellow, " (y/N)? ");
            return;
        }
        s.backToUsers = true;
        startForm(s, FormKind::UserEdit, now);
    }
}

// ---------------------------------------------------------------------------
// rowUsers: plain ASCII terminals get a paged list and the typed commands
// ---------------------------------------------------------------------------
bool Bbs::rowUsers(Session& s) {
    char buf[64];
    uint8_t i = s.listIdx++;
    if (i == 0) {
        snprintf(buf, sizeof(buf), "%u of %u", users::count(), syscfg::get().maxUsers);
        rowTitle(s, "Accounts", buf);
        return true;
    }
    if (i == 1) {
        rowText(s, Color::LightBlue, " Handle        Name             Calls");
        return true;
    }
    if (i == 2) {
        rowText(s, Color::DarkGrey, kMarkKey);
        return true;
    }
    static UserRec u;
    if (users::at(static_cast<uint8_t>(i - 3), u)) {
        char mark = u.level >= static_cast<uint8_t>(Access::Sysop) ? ']' : (u.level ? '>' : ' ');
        snprintf(buf, sizeof(buf), "%c%-13.13s %-16.16s %4u %c", mark, u.handle, u.name,
                 static_cast<unsigned>(u.calls), u.locked ? 'L' : ' ');
        rowText(s, Color::Grey, buf);
        return true;
    }
    if (s.listSub == 0) {
        s.listSub = 1;
        rowRule(s);
        return true;
    }
    if (s.listSub == 1) {
        s.listSub = 2;
        rowText(s, Color::DarkGrey, "USER ADD, USER EDIT h, USER DEL h");
        return true;
    }
    return false;
}
