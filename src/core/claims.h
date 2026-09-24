// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/claims.h
// Module:       Core / exclusive resources
//
// Purpose:      One caller at a time holds a shared board resource, and the
//               board can say who.
//
//               There were three hand-rolled versions of this idea before
//               this file existed, each invented where it was needed and
//               none of them aware of the others:
//
//                 g_cfgOwner            a Session*, "who is editing CONFIG"
//                 g_subjWho/g_subjFor   two uint8_t, "who filled the subject
//                                       table, and for which forum"
//                 the transfer engine   a bool, "somebody is transferring"
//
//               Three types, three scopes, three separate release paths that
//               each had to remember to clear themselves. One of them did
//               not, which is the recurring bug in this project: a claim
//               that outlives its owner. `pendingLand` dropped the next
//               caller on that node into the chat room; the squelch and away
//               masks leaked between callers; the subject table served one
//               caller's forum to another.
//
//               **Keyed by node, never by handle** (Rob). The same person
//               can be logged in on two lines at once, and they are two
//               callers as far as any resource is concerned. The sysop node
//               gets its own slot for the same reason.
//
// The rule that makes it work:
//
//               `releaseAll` runs in **openSession as well as
//               closeSession**. Sessions come from a static pool, so a claim
//               left behind is inherited by whoever dials in next, and a
//               stale claim is worse than no table at all: instead of a soft
//               weirdness the next caller gets a hard refusal for something
//               they never did. Clearing on the way in costs nothing and
//               cannot be forgotten by an exit path that did not run.
//
// Targets:      Host and ESP32
// See also:     CLAUDE.md
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
// ===========================================================================
#pragma once

#include <stdint.h>
#include "../config.h"

namespace claims {

// The shared things. Add a row here, not a new global somewhere.
enum class Res : uint8_t {
    Config = 0,        // the settings editor: one sysop at a time
    Transfer,          // the XMODEM/YMODEM engine: one board-wide
    Subjects,          // the forums' shared subject table
    Info,              // writing an information page: one at a time
    Count
};

// 0 means free. Otherwise it is the owning node id plus one, so that node 0
// is a real owner rather than indistinguishable from nobody.
inline uint8_t& slot(Res r) {
    static uint8_t owner[static_cast<uint8_t>(Res::Count)] = {};
    return owner[static_cast<uint8_t>(r)];
}

// held: is anybody holding it?
inline bool held(Res r) { return slot(r) != 0; }

// owner: which node, or 0xFF when free. Only for messages such as
// "Daytona is editing the settings"; never branch on it, use holds().
inline uint8_t owner(Res r) {
    uint8_t v = slot(r);
    return v ? static_cast<uint8_t>(v - 1) : 0xFF;
}

// holds: does THIS node hold it? False when nobody does.
inline bool holds(Res r, uint8_t node) {
    return slot(r) == static_cast<uint8_t>(node + 1);
}

// Two kinds of resource live in this table, and the difference is not
// cosmetic. It was found by migrating the third guard and watching a test
// fail: the second caller into a forum could not list its subjects.
//
//   A LOCK is exclusive. CONFIG and the transfer engine are locks: a second
//   caller must be refused and told why, because two sysops writing
//   system.cfg is how the file gets lost, and two transfers is a protocol
//   the engine does not speak. Use take(), and act on false.
//
//   A CACHE is a shared scratch space with a tag saying whose data is
//   currently in it. The forums' subject table is a cache: it holds one
//   forum's rows for whoever last asked, and the right answer when a second
//   caller wants it is to refill it for them, not to refuse. Use seize(),
//   which always succeeds.
//
// Both kinds want the same release discipline, which is the whole reason
// they share a table: releaseAll on the way into a session, so nothing is
// inherited from the last caller on that node.

// take: claim a LOCK. True if this node now holds it, which includes the
// case where it already did, so a re-entrant path does not have to check
// first. False means somebody else has it and the caller must say so.
inline bool take(Res r, uint8_t node) {
    uint8_t& o = slot(r);
    if (o == 0 || o == static_cast<uint8_t>(node + 1)) {
        o = static_cast<uint8_t>(node + 1);
        return true;
    }
    return false;
}

// seize: claim a CACHE. Always succeeds, because the previous holder's data
// is being replaced anyway and refusing would mean the second caller simply
// cannot use the feature. Whoever held it before finds it no longer tagged
// to them and refills it when they next look, which is what a cache does.
inline void seize(Res r, uint8_t node) {
    slot(r) = static_cast<uint8_t>(node + 1);
}

// release: give it up, but only if this node is the one holding it.
//
// The guard is the point. A blind release lets a caller who never held the
// resource free it out from under whoever does, and the resulting bug looks
// exactly like two people editing at once.
inline void release(Res r, uint8_t node) {
    uint8_t& o = slot(r);
    if (o == static_cast<uint8_t>(node + 1)) o = 0;
}

// releaseAll: this node is gone, or is arriving on a reused slot. Called
// from BOTH ends of a session's life. See the note at the top of this file:
// the arriving end is the one that actually prevents the bug.
inline void releaseAll(uint8_t node) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(Res::Count); ++i)
        release(static_cast<Res>(i), node);
}

// transfer: the same caller, a different node id.
//
// `Bbs::moveSession` carries a live caller to another slot when the sysop
// elevates or somebody is DROPped, and the id changes with them. Without
// this, anything they were holding stays filed under the node they left: the
// resource is never released, because the owner no longer matches, and it is
// never usable again until a reboot. That is the same "claim outlives its
// owner" shape this table exists to end, so it is handled here rather than
// left for whoever debugs it.
inline void transfer(uint8_t from, uint8_t to) {
    if (from == to) return;
    for (uint8_t i = 0; i < static_cast<uint8_t>(Res::Count); ++i) {
        Res r = static_cast<Res>(i);
        if (holds(r, from)) {
            slot(r) = static_cast<uint8_t>(to + 1);
        }
    }
}

}  // namespace claims
