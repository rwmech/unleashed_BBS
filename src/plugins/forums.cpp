/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/plugins/forums.cpp
 * Module:       Plugins / message boards
 *
 * Purpose:      Topic areas the sysop configures, subjects callers create
 *                  inside them, and messages inside those. Phase 1 is the
 *                  forum list and the file formats it stands on.
 *
 * Design:       Three levels of drill-down, and a fast path that skips all
 *               of them. Browsing is forum -> subject -> message; reading is
 *               one key that walks everything new wherever it lives, which is
 *               what a regular caller actually uses. See PLAN-FORUMS.md and
 *               internal/ux-message-boards.md.
 *
 * Notes:        PF_SD. No card, no plugin, and FORUMS is not a command at
 *               all, the same deal the file areas have. A board that offered
 *               empty message boards on hardware that cannot hold them would
 *               be worse than one that does not offer them.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLAN-FORUMS.md, internal/ux-message-boards.md, COMMANDS.md
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

#include <cstdio>
#include "../core/disk.h"              // fopen and opendir that tell the drive light (1.1.1)
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>                    // fsync: a record on the card before the header names it

#include "../config.h"
#include "../core/bbs.h"
#include "../core/claims.h"
#include "../core/bbs_util.h"
#include "../core/plugin.h"
#include "../core/clock.h"
#include "../core/compose.h"
#include "../core/codes.h"
#include "../core/sysconfig.h"
#include "../core/users.h"
#include "../core/fx.h"                // the walk's spinner (1.1.2)
#include "../core/runner.h"            // the one writer (1.2.1)
#include "../platform/platform.h"
#include "forums_ptr.h"

using namespace bbsu;

namespace {

// ===========================================================================
// The formats.
//
// These are frozen. Every later phase stands on them, and the one migration
// this design exists to avoid is rewriting INDEX.TXT on a card somebody
// already has messages on.
//
// The shape of the guarantee: **message N lives at byte offset N * 128 in
// INDEX.TXT, forever.** Nothing packs, compacts or rewrites the index; a
// deleted message keeps its slot and flips a flag. That is what lets a read
// pointer mean the same thing next year, and the usual way a pointer stops
// meaning anything is a well-meaning compaction pass.
// ===========================================================================

constexpr uint16_t kRec      = 128;   // bytes per index record, 4 to a sector
constexpr uint8_t  kSubject  = 49;    // subject characters in a record
constexpr uint8_t  kHandle   = 20;    // cached author handle, space padded
constexpr const char kMagic[] = "#UBF1";

// The record, field by field, as it is written. Kept as a table rather than
// as prose because the next person needs the offsets, not a description.
//
//   6  message number   decimal, redundant with the slot, a self-check
//   1  TAB
//   2  flags            [0] '.' live / 'X' deleted, [1] '.' normal / '!' pinned
//   1  TAB
//   8  author id        hex. the identity. never changes, never reused
//   1  TAB
//  20  author handle    space padded. a cached render, allowed to be stale
//   1  TAB
//  10  epoch
//   1  TAB
//   4  segment          which M####.TXT holds the body
//   1  TAB
//   6  offset           byte offset of the body within that segment
//   1  TAB
//   4  length           body bytes
//   1  TAB
//   8  subject hash     hex. what groups a conversation
//   1  TAB
//  49  subject          space padded
//   2  CR LF
// ---
// 128
//
// **The subject hash is paid for by the two vote fields**, which were 3+1+3+1
// and are gone: votes were dropped, and taking their bytes closes that door
// deliberately rather than leaving it ajar. One character came off the
// subject (50 to 49) to make up the ninth byte. Rob took that decision
// knowing it means a score can never appear in a listing without a format
// change on real cards.
//
// The handle stays at its full 20. Truncating an author's name in the one
// place a listing shows it is the `%9.9s` mistake that once printed this
// board's own address as "192.168.0".
constexpr uint16_t kOffNum     = 0;
constexpr uint16_t kOffFlags   = 7;
constexpr uint16_t kOffId      = 10;
constexpr uint16_t kOffHandle  = 19;
constexpr uint16_t kOffEpoch   = 40;
constexpr uint16_t kOffSeg     = 51;
constexpr uint16_t kOffOfs     = 56;
constexpr uint16_t kOffLen     = 63;
constexpr uint16_t kOffHash    = 68;
constexpr uint16_t kOffSubject = 77;
constexpr uint16_t kOffCrLf    = 126;

// The assert that matters, written the way the MailRec one had to be fixed
// to be written: **a format is offsets and a total, not a list of widths.**
// Summing the fields and comparing with the total is the version that fails
// on correct code the day padding appears, and that cost a round already.
static_assert(kOffCrLf + 2 == kRec, "index record is not 128 bytes");
static_assert(kOffSubject + kSubject == kOffCrLf, "subject does not reach the CR LF");
static_assert(kOffHandle + kHandle == kOffEpoch - 1, "handle field moved");
static_assert(kRec % 4 == 0, "records must stay aligned within a sector");

// ---------------------------------------------------------------------------
// subjectHash: what groups a conversation.
//
// Grouping is by hash and never by the display string. A reply carries its
// parent's hash forward, so renaming a subject, or disambiguating two that
// collide, cannot split a thread or merge two. That is the whole reason the
// hash is stored rather than recomputed from the text at read time.
//
// Case and surrounding space are folded out, because "20m Antennas" and
// "20m antennas " are the same conversation to everybody except a computer.
// FNV-1a: small, no table, and good enough for a space this size. It is not
// a security hash and nothing here pretends otherwise.
// ---------------------------------------------------------------------------
// The hash moved to bbsu::foldHash so the forums, the users.txt validator
// and the CONFIG page share one implementation. The stored format is
// unchanged: same algorithm, same folding, same 0 sentinel, so every forum
// already written still groups exactly as it did.
uint32_t subjectHash(const char* s) { return bbsu::foldHash(s); }

// ===========================================================================
// Configuration: the topic areas.
//
// The sysop configures these and callers cannot create them, exactly as file
// areas work. A forum with no config entry is not a forum.
// ===========================================================================

constexpr uint8_t kMaxForums = 16;    // where Form::kMaxFields puts the wall
constexpr uint8_t kKeyMax    = 12;    // folder name on the card
constexpr uint8_t kNameMax   = 24;
constexpr uint8_t kAboutMax  = 40;

struct Forum {
    char      key[kKeyMax + 1]   = {};   // folder under <sd>/p/forums/
    char      name[kNameMax + 1] = {};   // what a caller sees
    char      about[kAboutMax + 1] = {}; // one line, shown at 64 columns and up
    PlugLevel read  = PlugLevel::Nobody; // see it in the list at all
    PlugLevel start = PlugLevel::Nobody; // open a NEW subject
    PlugLevel reply = PlugLevel::Nobody; // add to an existing one
    PlugLevel mod   = PlugLevel::Nobody; // delete, pin, move
    uint32_t  newest = 0;                // highest message number, from the header
    // Two different quantities that were briefly one field, which put a
    // per-caller number into a board-wide file. `total` is how many live
    // messages the forum holds and belongs in the header; `unread` is how
    // many THIS caller has not read and must never be written to disk.
    // The header read back "count=1" on a forum with four messages because
    // the post path incremented the caller's number and then saved it.
    uint32_t  total  = 0;                // live messages, from the header
    // There was an `unread` here commented "for the caller on this session".
    // It was in THIS array, which is board-wide, so the second caller into
    // the forums overwrote the first caller's counts and the first then saw
    // somebody else's numbers on every redraw. The fourth comment in this
    // project to state what its code did not do. Per caller now: g_unread.
};

// The forums' colours. Deliberately the same shape as chat's: keys in the
// plugin's own config section, parsed by colorByName, defaulted here so a
// board that sets nothing still looks deliberate. A colour written as a
// literal in twenty places is a colour nobody can change.
Color g_cAsk    = Color::Yellow;      // a question waiting for an answer
Color g_cHead   = Color::LightGreen;  // a subject, wherever it is shown
Color g_cBody   = Color::White;       // the words of a message
Color g_cMeta   = Color::Grey;        // who wrote it and when
Color g_cMark   = Color::Cyan;        // the board's own voice, "--> "
Color g_cTitle  = Color::Cyan;        // the breadcrumb's fixed words and '>'
Color g_cSubj   = Color::White;       // the forum or subject name inside it
Color g_cCount  = Color::Yellow;      // a number a caller can act on: #412
Color g_cWho    = Color::LightGreen;  // who wrote it
Color g_cWhen   = Color::Grey;        // when, and "2 of 5"

Forum   g_forum[kMaxForums];
uint8_t g_forums = 0;
uint8_t g_index  = 0;
Bbs*    g_bbs    = nullptr;

// Per session: where the caller is standing.
enum class Where : uint8_t { Out, List };
Where   g_where[BBS_MAX_NODES + 2] = {};
uint8_t g_sel[BBS_MAX_NODES + 2]   = {};   // highlighted row on the forum list

uint8_t slotOf(const Session& s) { return s.id <= BBS_MAX_NODES + 1 ? s.id : 0; }

// ---------------------------------------------------------------------------
// This caller against this forum.
//
// Four levels, not two, and the split between `start` and `reply` is what
// makes a read-only announcements forum work BETTER than read-only:
// `read=all, start=co1, reply=users` is "staff post the news, anybody may
// answer it", which is a thing boards actually wanted and could not express.
//
// Each falls back to something chosen per permission rather than everything
// landing on the plugin's read level:
//
//   read  -> the plugin's read.  Seeing a forum is the plugin's own gate.
//   reply -> the plugin's write. Adding to a conversation is the ordinary act.
//   start -> THIS forum's reply. Opening a subject is at least as trusted as
//            answering one, and a forum that named one and not the other
//            means the stricter of the two.
//   mod   -> the plugin's ADMIN, never `start`. Deleting somebody else's
//            words is the destructive one, so it fails shut, the same
//            argument the file areas' `del` already settled.
// ---------------------------------------------------------------------------
bool mayRead(const Session& s, uint8_t i) {
    if (i >= g_forums || !g_forum[i].key[0]) return false;
    PlugLevel lv = g_forum[i].read;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 0);
    return plugins::mayUse(s, lv);
}

bool mayReply(const Session& s, uint8_t i) {
    if (i >= g_forums || !g_forum[i].key[0]) return false;
    PlugLevel lv = g_forum[i].reply;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 1);
    return plugins::mayUse(s, lv);
}

bool mayStart(const Session& s, uint8_t i) {
    if (i >= g_forums || !g_forum[i].key[0]) return false;
    PlugLevel lv = g_forum[i].start;
    if (lv == PlugLevel::Nobody) lv = g_forum[i].reply;          // this forum's
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 1);
    return plugins::mayUse(s, lv);
}

bool mayMod(const Session& s, uint8_t i) {
    if (i >= g_forums || !g_forum[i].key[0]) return false;
    PlugLevel lv = g_forum[i].mod;
    if (lv == PlugLevel::Nobody) lv = plugins::levelFor(g_index, 2);   // admin
    return plugins::mayUse(s, lv);
}

// visibleForums: the forums this caller may see, in order.
//
// One helper, used by the drawing and by the cursor keys alike. When each
// worked the list out for itself in the file areas, the highlight could sit
// on a different row than the one that opened, and a caller reads that as
// the board being broken rather than as an off-by-one.
uint8_t visibleForums(const Session& s, uint8_t* out, uint8_t max) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < g_forums && n < max; ++i)
        if (mayRead(s, i)) out[n++] = i;
    return n;
}

// ---------------------------------------------------------------------------
// Paths and the header.
// ---------------------------------------------------------------------------
// makeDirs: every component of a path under the card, parents first.
//
// mkdir does not create parents, so "p/forums/general" needs "p" and
// "p/forums" made first. A sysop sets a forum up from the board, not by
// pulling the card and finding a PC, which is the same argument the file
// areas settled.
void makeDirs(const char* full) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", full);
    size_t baseLen = strlen(plat::sdBase());
    for (char* q = buf + baseLen + 1; *q; ++q) {
        if (*q != '/') continue;
        *q = '\0';
        mkdir(buf, 0755);
        *q = '/';
    }
    mkdir(buf, 0755);
}

// Paths by the forum's key, not its slot (1.2.1): the writer on the runner
// works from the key it was handed, because a CONFIG save can renumber the
// topics while a write waits, and g_forum is the loop's.
void keyDir(const char* key, char* out, size_t n) {
    snprintf(out, n, "%s/p/forums/%s", plat::sdBase(), key);
}

void forumDir(uint8_t i, char* out, size_t n) { keyDir(g_forum[i].key, out, n); }

void indexPath(uint8_t i, char* out, size_t n) {
    char dir[96];
    forumDir(i, dir, sizeof(dir));
    snprintf(out, n, "%s/INDEX.TXT", dir);
}

// ---------------------------------------------------------------------------
// The header: record 0, the only record that is not a message.
//
// Tab separated key=value padded to 128 bytes, so a key can be added later
// without moving anything: nothing depends on where a field sits, only on
// the line being exactly 128 bytes. That is deliberate, and it is the one
// place in this format where a change later is cheap.
//
// seg= (1.2.1) is the first key added that way: the body segment the next
// post goes into, so a post opens that one segment instead of every full
// one from M0000 up (internal/fidonet-zmodem-2026-09-29.md, "found on the
// way"). A header written before 1.2.1 has none; the first post after the
// upgrade looks for the segment once, the old way, and writes it down.
// Firmware older than 1.2.1 reads a header with seg= and ignores the key.
// ---------------------------------------------------------------------------
constexpr uint16_t kSegLast = 9999;           // M9999.TXT: four digits in the record

struct Head {
    uint32_t count  = 0;
    uint32_t newest = 0;
    uint16_t seg    = 0;
    bool     hasSeg = false;
};

// headNum: a header value that is all digits up to its tab, false if not.
bool headNum(const char* s, uint32_t& v) {
    if (*s < '0' || *s > '9') return false;
    char* end = nullptr;
    const unsigned long n = strtoul(s, &end, 10);
    while (*end == ' ' || *end == '\r' || *end == '\n') ++end;   // the padding, the CR LF
    if (*end) return false;
    v = static_cast<uint32_t>(n);
    return true;
}

// parseHead: a 128-byte header record, false when it is not one. `size` is
// the index file's length: a header is believed only if it fits the file it
// heads (code review, 1.2.1-forums.3). newest= and count= are required and
// numeric, newest names a record the file holds, and count is at most
// newest. A missing or garbled newest parsed as 0, and the next post wrote
// over message 1; newest=999999 on a small file sent a post 128 MB into it
// and a recount through a million records. Anything else is torn, and a
// torn header is rebuilt from the records (runSeed).
bool parseHead(const char* rec, long size, Head& h) {
    h = Head();
    if (strncmp(rec, kMagic, sizeof(kMagic) - 1) != 0) return false;
    if (size < static_cast<long>(kRec)) return false;
    char buf[kRec + 1];
    memcpy(buf, rec, kRec);
    buf[kRec] = '\0';
    bool haveCount = false, haveNewest = false;
    for (char* p = buf; p && *p; ) {
        char* tab = strchr(p, '\t');
        if (tab) *tab = '\0';
        if (!strncmp(p, "count=", 6))  haveCount  = headNum(p + 6, h.count);
        if (!strncmp(p, "newest=", 7)) haveNewest = headNum(p + 7, h.newest);
        if (!strncmp(p, "seg=", 4)) {
            // Past M9999 is not a segment: a record's segment field has four
            // digits, and 34463 would be written as 3446, sending the body to
            // the wrong file (code review, 1.2.1-forums.2). Unknown, so the
            // next post looks for it.
            char* end = nullptr;
            const unsigned long v = strtoul(p + 4, &end, 10);
            if (end != p + 4 && v <= kSegLast) {
                h.seg    = static_cast<uint16_t>(v);
                h.hasSeg = true;
            }
        }
        p = tab ? tab + 1 : nullptr;
    }
    const uint32_t records = static_cast<uint32_t>(size / kRec - 1);
    return haveCount && haveNewest && h.newest <= records && h.count <= h.newest;
}

// Reading a header from an open index: RH_OK, RH_TORN (it came back and is
// not one), or RH_ERR (the card would not give it: never taken for torn).
enum : uint8_t { RH_OK = 0, RH_TORN, RH_ERR };
uint8_t headRead(FILE* ix, Head& h, long& size) {
    size = (fseek(ix, 0, SEEK_END) == 0) ? ftell(ix) : -1;
    if (size < 0 || fseek(ix, 0, SEEK_SET) != 0) return RH_ERR;
    char rec[kRec];
    const size_t got = fread(rec, 1, kRec, ix);
    if (got != kRec && ferror(ix)) return RH_ERR;
    return (got == kRec && parseHead(rec, size, h)) ? RH_OK : RH_TORN;
}

// buildHead: the header for a forum, exactly 128 bytes. Pads to 126 and
// closes with CR LF: a header that is not 128 bytes would put every message
// in the file at the wrong offset, which is the one failure this format
// cannot survive, so it is built to a fixed width rather than assembled and
// hoped over. seg= only when known: writing a guess would send the next post
// to the wrong segment.
void buildHead(char* rec, const char* key, const Head& h) {
    char tmp[kRec + 16];
    int n = h.hasSeg
        ? snprintf(tmp, sizeof(tmp), "%s\ttopic=%s\tcount=%06lu\tnewest=%06lu\tseg=%04u\t",
                   kMagic, key, static_cast<unsigned long>(h.count),
                   static_cast<unsigned long>(h.newest), static_cast<unsigned>(h.seg))
        : snprintf(tmp, sizeof(tmp), "%s\ttopic=%s\tcount=%06lu\tnewest=%06lu\t",
                   kMagic, key, static_cast<unsigned long>(h.count),
                   static_cast<unsigned long>(h.newest));
    if (n < 0) n = 0;
    if (n > kOffCrLf) n = kOffCrLf;
    memcpy(rec, tmp, static_cast<size_t>(n));
    for (size_t k = static_cast<size_t>(n); k < kOffCrLf; ++k) rec[k] = ' ';
    rec[kOffCrLf]     = '\r';
    rec[kOffCrLf + 1] = '\n';
}

// readHeader: the loop's copy of a forum's figures, at start. A read, never
// a write: the header has one writer, the runner (1.2.1).
bool readHeader(uint8_t i) {
    char path[128];
    indexPath(i, path, sizeof(path));
    FILE* f = disk::open(path, "rb");
    if (!f) return false;
    Head h;
    long size = 0;
    const uint8_t r = headRead(f, h, size);
    fclose(f);
    if (r != RH_OK) return false;               // the seed looks again, on the runner
    g_forum[i].total  = h.count;
    g_forum[i].newest = h.newest;
    return true;
}

// ===========================================================================
// Index records: one message, read and written.
//
// A record is built into a fixed 128 byte buffer field by field, never with
// one long snprintf. The reason is the guarantee the whole format rests on:
// message N lives at offset N * 128. A record that came out 127 bytes because
// a field was shorter than expected would put every message after it at the
// wrong offset, and nothing would notice until somebody read the file next
// year. Building into a padded buffer makes that impossible rather than
// unlikely.
// ===========================================================================

struct MsgRec {
    uint32_t num    = 0;
    bool     live   = true;
    bool     pinned = false;
    uint32_t authorId = 0;
    char     handle[kHandle + 1] = {};
    uint32_t epoch  = 0;
    uint16_t seg    = 0;
    uint32_t ofs    = 0;
    uint16_t len    = 0;
    uint32_t hash   = 0;
    char     subject[kSubject + 1] = {};
};

// putField: n characters at a fixed offset, space padded, never terminated.
//
// The record is not a C string. Writing a terminator into it would put a NUL
// in the middle of a text file that a sysop is expected to be able to read
// on a laptop, which is half the point of the format being text at all.
void putField(char* rec, uint16_t at, uint8_t n, const char* src) {
    uint8_t i = 0;
    for (; src && src[i] && i < n; ++i) rec[at + i] = src[i];
    for (; i < n; ++i) rec[at + i] = ' ';
}

void putNum(char* rec, uint16_t at, uint8_t n, uint32_t v, bool hex) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), hex ? "%0*lX" : "%0*lu",
             static_cast<int>(n), static_cast<unsigned long>(v));
    for (uint8_t i = 0; i < n; ++i) rec[at + i] = tmp[i] ? tmp[i] : '0';
}

void buildRec(const MsgRec& m, char* rec) {
    memset(rec, ' ', kRec);
    putNum(rec, kOffNum, 6, m.num, false);
    rec[kOffFlags]     = m.live ? '.' : 'X';
    rec[kOffFlags + 1] = m.pinned ? '!' : '.';
    putNum(rec, kOffId, 8, m.authorId, true);
    putField(rec, kOffHandle, kHandle, m.handle);
    putNum(rec, kOffEpoch, 10, m.epoch, false);
    putNum(rec, kOffSeg, 4, m.seg, false);
    putNum(rec, kOffOfs, 6, m.ofs, false);
    putNum(rec, kOffLen, 4, m.len, false);
    putNum(rec, kOffHash, 8, m.hash, true);
    putField(rec, kOffSubject, kSubject, m.subject);
    // Every field is followed by a tab, and the tabs are what make this
    // readable in a spreadsheet as well as in an editor.
    rec[kOffFlags - 1] = '\t';
    rec[kOffId - 1]      = '\t';
    rec[kOffHandle - 1]  = '\t';
    rec[kOffEpoch - 1]   = '\t';
    rec[kOffSeg - 1]     = '\t';
    rec[kOffOfs - 1]     = '\t';
    rec[kOffLen - 1]     = '\t';
    rec[kOffHash - 1]    = '\t';
    rec[kOffSubject - 1] = '\t';
    rec[kOffCrLf]     = '\r';
    rec[kOffCrLf + 1] = '\n';
}

void parseRec(const char* rec, MsgRec& m) {
    char buf[kSubject + 1];
    auto grab = [&](uint16_t at, uint8_t n) -> const char* {
        uint8_t i = n;
        while (i && rec[at + i - 1] == ' ') --i;    // right trim
        memcpy(buf, rec + at, i);
        buf[i] = '\0';
        return buf;
    };
    m.num      = strtoul(grab(kOffNum, 6), nullptr, 10);
    m.live     = rec[kOffFlags] != 'X';
    m.pinned   = rec[kOffFlags + 1] == '!';
    m.authorId = strtoul(grab(kOffId, 8), nullptr, 16);
    // %.*s, not %s: grab() returns a buffer sized for the widest field
    // it serves, so the compiler cannot see that a handle is only 20 of
    // it. Saying the bound here is free and keeps the build silent.
    snprintf(m.handle, sizeof(m.handle), "%.*s", kHandle, grab(kOffHandle, kHandle));
    m.epoch    = strtoul(grab(kOffEpoch, 10), nullptr, 10);
    m.seg      = static_cast<uint16_t>(strtoul(grab(kOffSeg, 4), nullptr, 10));
    m.ofs      = strtoul(grab(kOffOfs, 6), nullptr, 10);
    m.len      = static_cast<uint16_t>(strtoul(grab(kOffLen, 4), nullptr, 10));
    m.hash     = strtoul(grab(kOffHash, 8), nullptr, 16);
    snprintf(m.subject, sizeof(m.subject), "%s", grab(kOffSubject, kSubject));
}

// readRec: message n out of forum i. Returns false past the end.
//
// One seek and one read, because the offset is arithmetic rather than a
// search. That is the whole return on a fixed-width format: finding message
// 40,000 costs the same as finding message 2.
bool readRec(uint8_t i, uint32_t n, MsgRec& m) {
    if (!n) return false;                       // record 0 is the header
    char path[128];
    indexPath(i, path, sizeof(path));
    FILE* f = disk::open(path, "rb");
    if (!f) return false;
    bool ok = false;
    if (fseek(f, static_cast<long>(n) * kRec, SEEK_SET) == 0) {
        char rec[kRec];
        if (fread(rec, 1, kRec, f) == kRec) { parseRec(rec, m); ok = m.num == n; }
    }
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------
// Bodies: M####.TXT, appended to, never rewritten.
//
// The body file is separate from the index for one reason: the index has to
// stay a compact thing to scan. A 218 message forum is a 28 KB index, which
// is one sequential read; putting the bodies in it would make that 300 KB
// and every unread count would walk all of it.
//
// A segment rolls at kSegMax so no single file grows without bound on a FAT
// volume, and the record carries which segment it is in, so a rolled segment
// costs nothing to find.
// ---------------------------------------------------------------------------
constexpr uint32_t kSegMax  = 128u * 1024u;   // roll a body file at 128 KB
// The longest body the format can say, not a buffer size: the length field
// is four decimal digits. It was 1,728 (kBodyMax, 24 lines x 72), and a body
// had to fit a stack buffer that size to be read at all. The reader reads a
// body from the card in slices now (1.2.1), so the format's limit is the
// only one. A post written here is still at most BBS_COMPOSE_MAX (1,536):
// this is for bodies the board did not write, a foreign tosser's later.
constexpr uint16_t kBodyCap = 9999;

void segPath(const char* dir, uint16_t seg, char* out, size_t n) {
    snprintf(out, n, "%s/M%04u.TXT", dir, static_cast<unsigned>(seg));
}

// appendBody: the text into segment `seg` of the forum in `dir`, rolling on
// to the next segment when that one is full (seg comes back as the one
// used), and where it landed. The runner's (1.2.1).
//
// Returns false without writing anything if the card is full or the file
// cannot be opened, so the caller can refuse the post rather than write an
// index record pointing at a body that is not there. Order matters here:
// **the body is written and flushed before the index record that names it.**
// A power cut between the two leaves an orphan body, which is invisible and
// harmless; the other order leaves an index entry pointing at nothing, which
// is a message that exists and cannot be read.
//
// One open for the common post, two when it rolls. The segment the header
// names can be full (the post before filled it) but never further behind
// than that, so the roll is a step, never a search.
bool appendBody(const char* dir, const char* text, uint16_t n, uint16_t& seg, uint32_t& ofs) {
    char path[128];
    for (uint8_t tries = 0; tries < 3; ++tries) {
        segPath(dir, seg, path, sizeof(path));
        FILE* f = disk::open(path, "ab");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long at = ftell(f);
        if (at < 0) { fclose(f); return false; }
        if (static_cast<uint32_t>(at) >= kSegMax && seg < kSegLast) {
            fclose(f);                          // full: the next one
            ++seg;
            continue;
        }
        bool ok = fwrite(text, 1, n, f) == n;
        ok = ok && fputc('\n', f) != EOF;
        ok = fflush(f) == 0 && ok;
        if (fclose(f) != 0) ok = false;
        if (!ok) return false;
        ofs = static_cast<uint32_t>(at);
        return true;
    }
    return false;
}

// findSeg: the newest segment with room, looked for the way every post did
// before 1.2.1 (from M0000 up). Only for a header with no seg= in it, so
// once a forum, on the runner.
uint16_t findSeg(const char* dir) {
    char path[128];
    for (uint16_t s = 0; s < kSegLast; ++s) {
        segPath(dir, s, path, sizeof(path));
        FILE* f = disk::open(path, "rb");
        if (!f) return s;
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fclose(f);
        if (end >= 0 && static_cast<uint32_t>(end) < kSegMax) return s;
        if ((s & 15) == 15) runner::breathe();
    }
    return kSegLast;
}

// ===========================================================================
// The read pointer.
//
// Per caller, per forum: a high-water mark, plus a 16 byte window saying
// which messages ABOVE the mark have already been shown.
//
// The window is what makes a subject list honest. A single forum-wide mark
// cannot serve one: read a subject to its newest message and the mark would
// declare every older subject read too; refuse to move it and the subject
// still says "3 new" straight after somebody read all three. Either way the
// number on the screen is a lie, and it is the first thing a caller checks.
//
// **It self-drains**, which is the property that makes 16 bytes enough
// rather than merely small: as reading catches up, the mark advances through
// any bits already set, so the window empties itself instead of filling.
//
// The failure direction is the other half. A message read more than 128
// above the mark cannot be recorded, so it stays unread and is shown again.
// **It never marks an unread message read; it only forgets that one was
// read.** Of the two ways to be wrong, showing something twice is the one a
// caller forgives.
//
// Fixed width and 16 bytes exactly: this cannot be widened later without
// converting every PTRS.TXT on every card, which is the migration the whole
// format exists to avoid. Rob took that decision knowing it.
// ===========================================================================
// The window and the arithmetic over it live in forums_ptr.h, which has no
// board, card or session in it and is unit tested in host/test_forums_ptr.cpp
// (22 checks, including Rob's own interleaved-subject example walked through
// by hand). Only the on-disk part is here.
using forumptr::Ptr;
using forumptr::seen;
using forumptr::markSeen;
using forumptr::unreadUpTo;
using forumptr::kWindowBytes;

constexpr uint8_t kPtrRec = 6 + 1 + 32 + 1;        // "000412\t<32 hex>\t" = 40

void ptrPath(char* out, size_t n) {
    snprintf(out, n, "%s/p/forums/PTRS.TXT", plat::sdBase());
}

// Each caller's row is at their user id, which is why ids had to exist
// before this did. A handle can be renamed; an id never moves.
long ptrOffset(uint32_t userId, uint8_t forum) {
    return static_cast<long>(userId) * (kPtrRec * kMaxForums) +
           static_cast<long>(forum) * kPtrRec;
}

bool queuedPtr(uint32_t userId, uint8_t forum, Ptr& p);   // the writer, below

bool readPtr(uint32_t userId, uint8_t forum, Ptr& p) {
    p = Ptr{};
    if (!userId) return false;                     // guests keep no pointer
    // One on its way to the card is newer than the card (1.2.1): a caller who
    // leaves a forum and comes straight back must not be handed the pointer
    // from before, and be shown what they just read as new.
    if (queuedPtr(userId, forum, p)) return true;
    char path[128];
    ptrPath(path, sizeof(path));
    FILE* f = disk::open(path, "rb");
    if (!f) return false;
    bool ok = false;
    if (fseek(f, ptrOffset(userId, forum), SEEK_SET) == 0) {
        char rec[kPtrRec + 1] = {};
        if (fread(rec, 1, kPtrRec, f) == kPtrRec && rec[6] == '\t') {
            rec[6] = '\0';
            p.mark = strtoul(rec, nullptr, 10);
            for (uint8_t k = 0; k < kWindowBytes; ++k) {
                char hx[3] = { rec[7 + k * 2], rec[8 + k * 2], '\0' };
                p.win[k] = static_cast<uint8_t>(strtoul(hx, nullptr, 16));
            }
            ok = true;
        }
    }
    fclose(f);
    return ok;
}

// writePtr: the runner's (1.2.1), through the write queue below.
bool writePtr(uint32_t userId, uint8_t forum, const Ptr& p) {
    if (!userId) return true;                      // nothing to keep for a guest
    char path[128];
    ptrPath(path, sizeof(path));
    FILE* f = disk::open(path, "r+b");
    // "w+b" truncates, so only for a file that is not there. It was the
    // fallback for any failed "r+b", which on a card that refused one open
    // would have emptied every caller's read pointers in every forum. The
    // rule CLAUDE.md wrote down after the mail slots found it (1.1.2).
    if (!f && errno == ENOENT) f = disk::open(path, "w+b");
    if (!f) return false;

    char rec[kPtrRec + 1];
    snprintf(rec, sizeof(rec), "%06lu\t", static_cast<unsigned long>(p.mark));
    for (uint8_t k = 0; k < kWindowBytes; ++k)
        snprintf(rec + 7 + k * 2, 3, "%02X", p.win[k]);
    rec[kPtrRec - 1] = '\t';

    // A caller with a high id lands past the end of the file, and a seek
    // past the end followed by a write leaves a hole that reads back as
    // zeros. Zeros parse as a mark of 0 and an empty window, which is
    // exactly what somebody who has never read anything should have, so the
    // hole is correct rather than merely tolerated.
    bool ok = fseek(f, ptrOffset(userId, forum), SEEK_SET) == 0 &&
              fwrite(rec, 1, kPtrRec, f) == kPtrRec;
    ok = fflush(f) == 0 && ok;
    if (fclose(f) != 0) ok = false;
    return ok;
}

// ===========================================================================
// One writer for every forum file (1.2.1).
//
// Every write the forums make, a post, a removal, a read pointer, a new
// forum's header, goes into this one queue, and one job of this plugin's on
// the background runner makes them, one at a time, in the order they came.
// The loop never writes a forum file.
//
// Why, in order of how much it matters:
//
// - **One writer.** CONFIG_FATFS_FS_LOCK is 0, so two tasks writing one FAT
//   file corrupt it (runner.h). The forums had one writer, the loop, only
//   because nothing else wrote them; the FidoNet tosser planned for 1.4.0
//   is a second one, on the runner, writing the same INDEX.TXT and M####.TXT.
//   It joins this queue rather than racing it, the photos' FILES.BBS shape
//   (files.cpp, "One writer for the photos' FILES.BBS").
// - **Rule no. 1.** A post was three to four card opens and writes on the
//   loop, plus one open for every full body segment the forum held (the
//   segment search this build also removes). On the runner the loop pays
//   none of it: every other caller carries on while the card writes.
//
// What the poster sees: the post is confirmed when the runner has written
// it, not in the same pass. The job normally runs within a pass or two (a
// few milliseconds of card time), and after 250 ms the line shows the
// spinner every other wait on the board shows. It can wait longer behind
// another runner job: the runner runs one job at a time, so a camera snap
// under way (seconds; a UXGA snap on the Freenove is about 14 s) holds a
// post until it ends. After 10 s ESC or Q hands the caller back and the post
// still lands. Nothing is lost by waiting: the text was copied when it was
// queued.
//
// Readers stay on the loop and need no lock: the index record for N+1 is
// written (and fsync'd) before the header or the loop's `newest` moves to
// N+1, and a reader never looks past `newest`. A reader's own FAT file
// object may hold a stale copy of a sector the runner just wrote; the only
// thing that can be stale in it is a record's live flag, read a moment
// before a removal, which is the answer the caller would have had a moment
// earlier anyway.
//
// Read-your-own-write: a post's figures (its number, the forum's newest and
// count) come back with it and are put into g_forum before the poster is
// told, so the message is there to read the moment "Posted" is on screen. A
// read pointer still in the queue is found by readPtr before the card is.
//
// The queue is on the heap only while writes wait (1 KB, PSRAM where the
// board has it), so it costs no static DRAM, which the ESP32-CAM does not
// have. A post's text is copied into a block of its own for the same reason.
// ===========================================================================
enum : uint8_t { OP_POST = 1, OP_REMOVE, OP_PTR, OP_SEED };
enum : uint8_t { RES_OK = 0, RES_IO, RES_GONE };

// A post as the runner writes it: the record's fields and the text after
// the struct, in one block.
struct PostData {
    MsgRec   m;
    uint16_t len = 0;
    char*       body()       { return reinterpret_cast<char*>(this + 1); }
    const char* body() const { return reinterpret_cast<const char*>(this + 1); }
};

struct Op {
    uint8_t   kind  = 0;
    uint8_t   forum = 0xFF;          // the topic slot when queued: PTRS.TXT's column
    uint8_t   node  = 0xFF;          // the slot waiting for the answer, 0xFF nobody
    uint8_t   res   = RES_IO;        // the runner's answer
    uint16_t  call  = 0;             // Session::call of whoever waits: never a Session*
    uint16_t  ticket = 0;            // which of their waits: see g_ticket
    char      key[kKeyMax + 1] = {}; // the forum's folder: the runner works from this
    bool      head  = false;         // the runner read the header: newest/total mean something
    bool      recount = false;       // SEED: count the live records even if the header reads
    bool      stale = false;         // the runner's: the header on the card may be wrong, recount it
    uint32_t  num   = 0;             // REMOVE: which; POST: the number it got
    uint32_t  user  = 0;             // PTR: whose
    Ptr       ptr;                   // PTR: the pointer
    PostData* post  = nullptr;       // POST: the loop frees it when the answer is in
    uint32_t  newest = 0;            // the header after the op
    uint32_t  total  = 0;
};
static_assert(std::is_trivially_copyable<Op>::value, "an Op is copied in and out under the lock");

constexpr uint8_t kOps = 16;         // twelve callers' pointers at a CONFIG save, and posts beside them
Op*         g_ops     = nullptr;     // under the runner's lock
uint8_t     g_opHead  = 0;           // under the runner's lock
uint8_t     g_opCount = 0;           // under the runner's lock: queued, done or not
uint8_t     g_opDone  = 0;           // under the runner's lock: of those, from the head, written
runner::Job g_opJob;
// A number for each write a caller waits on, kept in their W_WRITE walk. The
// node and the call say it is still the same caller; this says it is still
// the same wait. A caller who stopped waiting (ESC after 10 s) and posted
// again would otherwise be told the first post's number for the second.
uint16_t    g_ticket  = 0;           // the loop's
uint16_t nextTicket() { if (!++g_ticket) ++g_ticket; return g_ticket; }

// The queue's memory, the way the photos' description queue has it.
void* opAlloc(size_t n) {
#if defined(BBS_HAS_CAMERA)
    return plat::camAlloc(n);
#elif defined(BBS_HAS_LCD)
    return plat::psramAlloc(n);
#else
    return malloc(n);
#endif
}
void opFree(void* p) {
    if (!p) return;
#if defined(BBS_HAS_CAMERA)
    plat::camFree(p);
#elif defined(BBS_HAS_LCD)
    plat::psramFree(p);
#else
    free(p);
#endif
}

bool queuedPtr(uint32_t userId, uint8_t forum, Ptr& p) {
    bool found = false;
    plat::runLock();
    if (g_ops) {
        for (uint8_t k = 0; k < g_opCount; ++k) {          // the newest one wins
            const Op& o = g_ops[(g_opHead + k) % kOps];
            if (o.kind == OP_PTR && o.user == userId && o.forum == forum) { p = o.ptr; found = true; }
        }
    }
    plat::runUnlock();
    return found;
}

// ---------------------------------------------------------------------------
// The runner's half. Nothing here reads g_forum, a Session or the timeline.
// ---------------------------------------------------------------------------

void runSeed(Op& op);

// headWrite: the header into an open index, which it closes. A write that
// fails is tried once more through a fresh open: FatFs keeps a write error
// on the file object, so a second try on the same one cannot succeed (code
// review, 1.2.1-forums.3).
bool headWrite(FILE* ix, const char* path, const char* key, const Head& h) {
    char rec[kRec];
    buildHead(rec, key, h);
    bool ok = fseek(ix, 0, SEEK_SET) == 0 && fwrite(rec, 1, kRec, ix) == kRec;
    ok = fflush(ix) == 0 && ok;
    if (fclose(ix) != 0) ok = false;
    if (ok) return true;
    FILE* f = disk::open(path, "r+b");
    if (!f) return false;
    ok = fwrite(rec, 1, kRec, f) == kRec;        // "r+b" opens at 0
    ok = fflush(f) == 0 && ok;
    if (fclose(f) != 0) ok = false;
    return ok;
}

// runPost: body, record, header, in that order, two opens (three when the
// segment rolls). The header is read from the card, not from RAM: the card
// is the one truth when there is one writer, and the number a post takes is
// the header's newest plus one at the moment it is written.
void runPost(Op& op) {
    const PostData* pd = op.post;
    if (!pd) return;
    char dir[96], path[128];
    keyDir(op.key, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/INDEX.TXT", dir);
    FILE* ix = disk::open(path, "r+b");
    if (!ix && errno == ENOENT) {
        // No forum yet (its seed was refused, or has not run): make it now,
        // as the seed would, rather than refuse the post.
        Op seed = op;
        runSeed(seed);
        if (seed.res == RES_OK) ix = disk::open(path, "r+b");
    }
    if (!ix) return;
    Head h;
    long size = 0;
    uint8_t got = headRead(ix, h, size);
    if (got == RH_TORN) {
        // A torn header is rebuilt from the records, as a seed would at
        // start, and the post goes on after them: never over message 1.
        fclose(ix);
        Op seed = op;
        runSeed(seed);
        ix = seed.res == RES_OK ? disk::open(path, "r+b") : nullptr;
        if (!ix) return;
        got = headRead(ix, h, size);
    }
    if (got != RH_OK) { fclose(ix); return; }
    op.head   = true;
    op.newest = h.newest;
    op.total  = h.count;

    uint16_t seg = h.hasSeg ? h.seg : findSeg(dir);
    uint32_t ofs = 0;
    if (!appendBody(dir, pd->body(), pd->len, seg, ofs)) { fclose(ix); return; }

    char rec[kRec];
    MsgRec m = pd->m;
    m.num = h.newest + 1;
    m.seg = seg;
    m.ofs = ofs;
    m.len = pd->len;
    buildRec(m, rec);
    bool ok = fseek(ix, static_cast<long>(m.num) * kRec, SEEK_SET) == 0 &&
              fwrite(rec, 1, kRec, ix) == kRec && fflush(ix) == 0;
    // The record on the card before the header names it. They are one open
    // now, where they were two, and a header that reached the card first
    // would name a record a power cut had lost. A sync that fails writes no
    // header: the record stays unnamed and the post is said not to have saved.
    if (ok) ok = fsync(fileno(ix)) == 0;
    if (!ok) { fclose(ix); return; }
    Head nh = h;
    nh.newest = m.num;
    nh.count  = h.count + 1;
    nh.seg    = seg;
    nh.hasSeg = true;
    if (!headWrite(ix, path, op.key, nh)) {
        // The record is on the card and unnamed, and the header may be half
        // one thing and half the other: said as not saved (nobody can read
        // it), and recounted from the card when the answer is handed out, so
        // a header left torn is rebuilt now rather than at the next post
        // (code review, 1.2.1-forums.3).
        op.stale = true;
        plat::log("forums: %s: a post's header did not save; recounting", op.key);
        return;
    }
    op.num    = m.num;
    op.newest = m.num;
    op.total  = h.count + 1;
    op.res    = RES_OK;
}

// runRemove: one byte, the live flag, then the header's live count.
//
// Once the flag is on the card (written and synced) the message IS removed,
// whatever happens to the header after it, so that is what the moderator is
// told (code review, 1.2.1-forums.2). A header that will not take the new
// count, twice (the second through a fresh open), is marked stale and
// recounted from the records by a seed op queued when the answer is handed
// out: the card's count feeds the "nothing was ever removed" shortcut, and
// one too high would count the removed message as unread for every caller
// (the 0.21.7 shape). A flag that did not sync is said as not saved, and
// recounted too, because it may still have reached the card. A torn header
// is recounted, which rebuilds it.
void runRemove(Op& op) {
    char path[128];
    char dir[96];
    keyDir(op.key, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/INDEX.TXT", dir);
    FILE* ix = disk::open(path, "r+b");
    if (!ix) return;
    Head h;
    long size = 0;
    const uint8_t got = headRead(ix, h, size);
    if (got != RH_OK) {
        fclose(ix);
        if (got == RH_TORN) op.stale = true;
        return;
    }
    op.head   = true;
    op.newest = h.newest;
    op.total  = h.count;
    char rec[kRec];
    MsgRec m;
    bool ok = op.num && op.num <= h.newest &&
              fseek(ix, static_cast<long>(op.num) * kRec, SEEK_SET) == 0 &&
              fread(rec, 1, kRec, ix) == kRec;
    if (ok) { parseRec(rec, m); ok = m.num == op.num; }
    if (!ok)     { fclose(ix); return; }
    if (!m.live) { fclose(ix); op.res = RES_GONE; return; }      // another moderator was first

    bool down = fseek(ix, static_cast<long>(op.num) * kRec + kOffFlags, SEEK_SET) == 0 &&
                fputc('X', ix) != EOF && fflush(ix) == 0;
    if (down) down = fsync(fileno(ix)) == 0;
    if (!down) {
        fclose(ix);
        op.stale = true;                         // the X may have landed all the same
        return;
    }
    if (h.count) --h.count;                      // the header's count is LIVE messages
    const bool headed = headWrite(ix, path, op.key, h);
    op.total = h.count;                          // the truth: the flag is down
    op.res   = RES_OK;
    if (!headed) {
        op.stale = true;
        plat::log("forums: %s: #%lu removed, but the header did not save; recounting",
                  op.key, static_cast<unsigned long>(op.num));
    }
}

// countLive: the live records 1..newest, read front to back, on the runner.
// -1 on a read error; a file that ends early counts what it holds. newest is
// bounded by the file's size (parseHead, or the rebuild's own arithmetic),
// so this never walks records the file does not have.
long countLive(FILE* ix, uint32_t newest) {
    if (fseek(ix, kRec, SEEK_SET) != 0) return -1;
    long live = 0;
    char rec[kRec];
    for (uint32_t n = 1; n <= newest; ++n) {
        if (fread(rec, 1, kRec, ix) != kRec) return ferror(ix) ? -1 : live;
        MsgRec m;
        parseRec(rec, m);
        if (m.num == n && m.live) ++live;
        if ((n & 63) == 0) runner::breathe();
    }
    return live;
}

// runSeed: a forum's folder and header, when start() found none. Does
// nothing to a forum whose header reads (start's read may have failed for a
// reason that has passed), unless asked to recount it (op.recount, after a
// write whose header did not save).
//
// A header that is torn (it came back without the magic, or with newest= or
// count= missing, garbled or larger than the file holds, or the file is
// shorter than a header) is rebuilt from the file: newest from its size, so
// the next post cannot land over message 1, and the count from the live
// flags, so the "nothing was ever removed" shortcut stays honest. A read
// ERROR is never taken for a torn header (code review, 1.2.1-forums.2): a
// card that failed one read must not have its good header replaced.
void runSeed(Op& op) {
    char dir[96], path[128];
    keyDir(op.key, dir, sizeof(dir));
    makeDirs(dir);
    snprintf(path, sizeof(path), "%s/INDEX.TXT", dir);
    FILE* ix = disk::open(path, "r+b");
    const int why = ix ? 0 : errno;
    Head h;
    bool counted = false, good = false;
    if (!ix) {
        if (why != ENOENT) return;
        ix = disk::open(path, "w+b");
        if (!ix) return;
        h.hasSeg = true;                         // a new forum: segment 0
    } else {
        long size = 0;
        const uint8_t got = headRead(ix, h, size);
        if (got == RH_ERR) {
            fclose(ix);
            plat::log("forums: %s: the header would not read; left as it is", op.key);
            return;
        }
        good = got == RH_OK;
        if (good && !op.recount) {
            fclose(ix);
            op.head = true;
            op.newest = h.newest;
            op.total  = h.count;
            op.res    = RES_OK;
            return;
        }
        if (!good) {
            h = Head();                          // seg= unknown: the next post looks for it
            if (size >= static_cast<long>(2 * kRec)) h.newest = static_cast<uint32_t>(size / kRec - 1);
        }
        const long live = countLive(ix, h.newest);
        if (live < 0) {
            fclose(ix);
            plat::log("forums: %s: the records would not read; the header is left as it is", op.key);
            return;
        }
        h.count = static_cast<uint32_t>(live);
        counted = true;
    }
    if (!headWrite(ix, path, op.key, h)) return;
    // Said once it is on the card, not before (code review, 1.2.1-forums.3):
    // a test reading the card at this line must find the header it names.
    if (counted)
        plat::log("forums: %s: %s the header, %lu records, %lu live", op.key,
                  good ? "recounted" : "rebuilt",
                  static_cast<unsigned long>(h.newest), static_cast<unsigned long>(h.count));
    op.head   = true;
    op.newest = h.newest;
    op.total  = h.count;
    op.res    = RES_OK;
}

void opRun(Op& op) {
    op.res = RES_IO;
    switch (op.kind) {
        case OP_POST:   runPost(op);   break;
        case OP_REMOVE: runRemove(op); break;
        case OP_PTR:    op.res = writePtr(op.user, op.forum, op.ptr) ? RES_OK : RES_IO; break;
        case OP_SEED:   runSeed(op);   break;
        default:        break;
    }
}

// opsWork: every op not yet written, in order. An op stays in its slot while
// it is written: the loop only takes done ones off the head and adds at the
// tail, so the first op not done is always at head + done.
void opsWork(runner::Job&) {
    for (;;) {
        plat::runLock();
        if (!g_ops || g_opDone >= g_opCount) { plat::runUnlock(); break; }
        const uint8_t at = static_cast<uint8_t>((g_opHead + g_opDone) % kOps);
        Op op = g_ops[at];
        plat::runUnlock();
        opRun(op);
        plat::runLock();
        g_ops[at] = op;
        ++g_opDone;
        plat::runUnlock();
        runner::breathe();
    }
}

// ---------------------------------------------------------------------------
// The loop's half.
// ---------------------------------------------------------------------------

// opsKick: the job out, if it is not. Called as an op is queued, so a write
// starts the pass it was asked for, and from the tick for anything left.
void opsKick() {
    if (runner::done(g_opJob)) runner::collect(g_opJob);
    if (!runner::idle(g_opJob)) return;
    plat::runLock();
    const bool waiting = g_ops && g_opDone < g_opCount;
    plat::runUnlock();
    if (!waiting) return;
    g_opJob.work = opsWork;
    g_opJob.name = "forum writes";
    runner::post(g_opJob);          // refused: the tick asks again
}

// opQueue: an op on the end of the queue, false when it will not fit (the
// caller says so). The ring is made outside the lock (nothing is allocated
// inside a critical section) and taken inside it only if nobody made one
// meanwhile, the photos' queue's shape.
bool opQueue(const Op& op) {
    Op* spare = nullptr;
    bool room = false, have = false;
    for (uint8_t tries = 0; tries < 2 && !have; ++tries) {
        plat::runLock();
        if (!g_ops && spare) { g_ops = spare; spare = nullptr; g_opHead = 0; g_opCount = 0; g_opDone = 0; }
        if (g_ops) {
            have = true;
            // A read pointer may not take the last two slots: twelve of them
            // at a CONFIG save must not be what refuses somebody's post. A
            // lost pointer costs a caller seeing messages as new again; a
            // refused seed would leave a new forum unable to take a post.
            room = g_opCount < (op.kind == OP_PTR ? kOps - 2 : kOps);
            if (room) {
                g_ops[(g_opHead + g_opCount) % kOps] = op;
                ++g_opCount;
            }
        }
        plat::runUnlock();
        if (have) break;
        void* mem = opAlloc(sizeof(Op) * kOps);
        if (!mem) break;
        spare = static_cast<Op*>(mem);
        for (uint8_t i = 0; i < kOps; ++i) new (&spare[i]) Op();
    }
    opFree(spare);
    if (!have) plat::log("forums: a write was not queued: no memory for the queue");
    else if (!room) plat::log("forums: a write was not queued: the queue is full");
    if (room) opsKick();
    return room;
}

void opDeliver(Op& op);    // below, with the walks it finishes

// opsTick: the answers that are in, handed out, then the job out again for
// anything still waiting, or the ring back to the heap when nothing is.
void opsTick() {
    if (runner::done(g_opJob)) runner::collect(g_opJob);
    for (uint8_t k = 0; k < kOps; ++k) {
        Op op;
        bool got = false;
        plat::runLock();
        if (g_ops && g_opDone) {
            op = g_ops[g_opHead];
            g_ops[g_opHead] = Op();
            g_opHead = static_cast<uint8_t>((g_opHead + 1) % kOps);
            --g_opCount;
            --g_opDone;
            got = true;
        }
        plat::runUnlock();
        if (!got) break;
        opDeliver(op);
    }
    if (!runner::idle(g_opJob)) return;
    plat::runLock();
    Op* drop = nullptr;
    if (g_ops && !g_opCount) { drop = g_ops; g_ops = nullptr; g_opHead = 0; g_opDone = 0; }
    plat::runUnlock();
    opFree(drop);
    opsKick();
}

// ---------------------------------------------------------------------------
// Config parsing.
//
// **In parser order.** The file areas shipped with CLAUDE.md documenting the
// order as read|down|up|del while readKey parsed read|up|down|del, so a
// sysop following the documentation set the download level where the upload
// level goes. Same six fields, two different bugs in one afternoon, one in
// the editor and one in the prose. The order here is written once, in the
// parser, and the docs are checked against this function rather than against
// anybody's memory of it.
//
//   topic1 = key | name | about | read | start | reply | mod
// ---------------------------------------------------------------------------
void readKey(void* ctx, const char* key, const char* value) {
    (void)ctx;
    if (strncmp(key, "topic", 5) != 0) return;
    long n = strtol(key + 5, nullptr, 10);
    if (n < 1 || n > kMaxForums) return;

    char parts[7][64] = {};
    uint8_t np = 0;
    const char* p = value;
    while (np < 7) {
        const char* bar = strchr(p, '|');
        size_t len = bar ? static_cast<size_t>(bar - p) : strlen(p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) --len;
        while (len && (*p == ' ' || *p == '\t')) { ++p; --len; }
        snprintf(parts[np], sizeof(parts[0]), "%.*s", static_cast<int>(len), p);
        ++np;
        if (!bar) break;
        p = bar + 1;
    }
    if (!parts[0][0]) return;

    Forum f;
    // The key is a folder name on a FAT card. Anything that is not a plain
    // name is refused rather than sanitised: a folder created under a name
    // nobody typed is harder to notice than a forum that plainly did not
    // appear. Long names work (CONFIG_FATFS_LFN_HEAP) but a path separator
    // or a dot-dot is a different question entirely.
    for (const char* c = parts[0]; *c; ++c) {
        bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                  (*c >= '0' && *c <= '9') || *c == '_' || *c == '-';
        if (!ok) {
            plat::log("forums: topic%ld key '%s' is not a plain name, ignored",
                      n, parts[0]);
            return;
        }
    }
    snprintf(f.key,  sizeof(f.key),  "%.*s", kKeyMax,  parts[0]);
    snprintf(f.name, sizeof(f.name), "%.*s", kNameMax,
             parts[1][0] ? parts[1] : parts[0]);
    snprintf(f.about, sizeof(f.about), "%.*s", kAboutMax, parts[2]);

    // A level the ladder does not recognise is refused and logged rather
    // than guessed at, so a typo leaves the forum on its fallback instead of
    // quietly opening it to everybody.
    struct { uint8_t at; PlugLevel* into; const char* what; } lv[] = {
        { 3, &f.read,  "read"  },
        { 4, &f.start, "start" },
        { 5, &f.reply, "reply" },
        { 6, &f.mod,   "mod"   },
    };
    for (auto& e : lv) {
        if (np > e.at && parts[e.at][0] &&
            !plugins::levelFromText(parts[e.at], *e.into))
            plat::log("forums: topic%ld %s level '%s' is not a level, using the fallback",
                      n, e.what, parts[e.at]);
    }

    uint8_t i = static_cast<uint8_t>(n - 1);
    g_forum[i] = f;
    if (i + 1 > g_forums) g_forums = static_cast<uint8_t>(i + 1);
}

// ===========================================================================
// Lifecycle.
// ===========================================================================

// start: idempotent, because it is NOT only the boot path.
//
// Saving any CONFIG page stops and starts every plugin, with callers on the
// line. The sd plugin already carries this scar: its start() used to unmount
// and remount the card, so changing idle_minutes stalled every caller for a
// full SPI renegotiation. Nothing here may do synchronous work that a config
// save should not pay for.
// The walks and the read pointers in RAM (1.1.2), defined with the walks
// below: start clears them, stop writes the pointers out.
void walkResetAll();
void walkFlushAll();
void unreadNow(Session& s, uint8_t forum);
void jumpFound(Bbs& b, Session& s, uint32_t num);

bool start(Bbs& bbs) {
    g_bbs   = &bbs;
    g_index = plugins::indexOf("forums");
    g_forums = 0;
    for (auto& f : g_forum) f = Forum{};

    plugins::forEachKey(g_index, readKey, nullptr);

    // Each forum's figures from its header. A forum with no header yet (a
    // new one, or one whose read failed) gets its folder and header from the
    // writer (1.2.1): start() writes nothing to the card itself. The seed
    // does nothing to a header it finds readable, which is what makes this
    // safe to run on every config save, and its answer puts the figures in.
    for (uint8_t i = 0; i < g_forums; ++i) {
        if (!g_forum[i].key[0]) continue;
        if (readHeader(i)) continue;
        Op op;
        op.kind  = OP_SEED;
        op.forum = i;
        snprintf(op.key, sizeof(op.key), "%s", g_forum[i].key);
        if (!opQueue(op))
            plat::log("forums: could not queue the header for %s", g_forum[i].key);
    }

    memset(g_where, 0, sizeof(g_where));
    memset(g_sel, 0, sizeof(g_sel));
    walkResetAll();
    return true;
}

void stop() {
    // No card work but one: a read pointer still in RAM is queued for the
    // writer, so a CONFIG save (which stops and starts every plugin) loses
    // nobody's place (1.1.2). The mount is board-level state and stays.
    // The writes already queued go on without the plugin: the runner owns
    // them now, and their answers are handed out at the next start's tick.
    walkFlushAll();
    opsKick();
    g_bbs = nullptr;
}

// ===========================================================================
// The three levels, and the one key that skips all of them.
//
// Browsing is forum -> subject -> message. Reading is Enter, which walks
// everything unread wherever it lives and never asks the caller to choose.
// Both exist on purpose: the drill-down is for somebody hunting the 20m
// antennas subject specifically, and the fast path is what a regular caller
// uses every single call. If the drill-down were the only way in, this would
// be slower than the flat design it replaced and people would stop calling.
// ===========================================================================

enum class View : uint8_t { Forums, Subjects, Reading };

// gap: start with a newline. True after a body or a notice, where it gives
// the blank line before the footer; false straight after a closing rule,
// where a blank line would only separate the rule from what it closes.
void prompt(Bbs& b, Session& s, bool gap = true);
void say(Session& s, Color c, const char* text);
void notice(Session& s, Color c, const char* text);
void noticeDone(Bbs& b, Session& s);
void listStatus(Session& s, uint32_t unread, const char* none);
void answered(Session& s, const char* text);

View     g_view[BBS_MAX_NODES + 2]  = {};
uint8_t  g_at[BBS_MAX_NODES + 2]    = {};    // current forum, 0xFF = none
uint32_t g_subj[BBS_MAX_NODES + 2]  = {};    // current subject hash, 0 = all
uint32_t g_shown[BBS_MAX_NODES + 2] = {};    // message on screen right now

// What each caller has not read, per forum. Per caller because it is a per
// caller number: it used to live in the board-wide Forum table, where two
// callers in the forums at once overwrote each other's. uint16_t because a
// count is shown, not summed into anything that could overflow, and 65,535
// unread in one forum is not a state a caller reads their way out of.
uint16_t g_unread[BBS_MAX_NODES + 2][kMaxForums] = {};

inline void setUnread(uint8_t sl, uint8_t i, uint32_t n) {
    g_unread[sl][i] = static_cast<uint16_t>(n > 0xFFFFu ? 0xFFFFu : n);
}

// The subject tally a listing is built from. One slot per subject that has
// something to say, capped: a forum with more distinct subjects than a
// screen can hold is paged by the core's list machinery, not held whole.
constexpr uint8_t kMaxSubjects = 64;

struct SubjRow {
    uint32_t hash   = 0;
    // The subject's number: the ID of the oldest surviving message in it.
    // Rob, looking at "1 Wrapping test" in the list and "#4" on the message:
    // "It shows 1 above, but 4 below, which is it?" The list numbered ROWS,
    // so a subject's number moved whenever another subject came or went and
    // never matched anything on the message itself. The index is never
    // compacted, so a message's ID is permanent, and numbering a subject by
    // its first message means the number in the list is the number the
    // message shows when it opens. If a sysop removes the opening post the
    // subject takes the next one's ID, which still names a message a caller
    // can open.
    uint32_t first  = 0;
    uint32_t newest = 0;
    uint32_t unread = 0;
    uint32_t total  = 0;
    char     subject[kSubject + 1] = {};
};

// ONE table, shared by every caller, and that is a deliberate trade rather
// than an oversight. Per-session tables would be 12 x 64 rows of about 66
// bytes, roughly 53 KB of static DRAM that is empty almost always, on a
// board that has already overflowed its DRAM once this week.
//
// The cost of sharing is that the table belongs to whoever filled it last,
// so it is TAGGED with that caller and that forum. A caller who finds
// somebody else's data rescans for their own. Without the tag, two callers
// in two forums hand each other subject hashes and a number key silently
// does nothing, which reads as a broken board rather than as a collision.
SubjRow g_subjRow[kMaxSubjects];
uint8_t g_subjRows = 0;
uint8_t g_subjFor  = 0xFF;      // which forum the table describes
// Who filled it is claims::Res::Subjects, keyed by node, so it is released
// from openSession whatever happened on the way out. g_subjFor stays here
// because "which forum" is not ownership: two callers in the same forum
// still cannot share the table, since only one of them filled it.

// ---------------------------------------------------------------------------
// scanSubjects: one backward pass over INDEX.TXT, grouping by hash.
//
// Backward from the newest message, because the newest is what a caller
// cares about and because stopping early then leaves the most interesting
// subjects rather than the oldest. The scan reads whole records and never
// searches, since the offset of message N is arithmetic.
//
// Bounded twice: by the number of distinct subjects a screen can use, and by
// how far back it will walk at all. An unbounded scan of a forum with a
// million messages would be a synchronous multi-second read on the caller's
// own path, which is the shape that produced a measured 126 ms stall in the
// file listing and would be far worse here.
// ---------------------------------------------------------------------------
constexpr uint32_t kScanMax = 2000;         // records walked for a listing

// Digits in the widest subject number in the table, so the list's number
// column lines up however large the IDs get.
uint8_t g_subjNumW = 1;

// scanReset / scanAdd / scanDone: scanSubjects in three parts (1.1.2), for
// the W_SCAN walk, which fills the table a slice at a time.
void scanReset(uint8_t i, uint8_t who) {
    g_subjRows = 0;
    g_subjNumW = 1;
    g_subjFor  = i;
    claims::seize(claims::Res::Subjects, who);   // a cache, not a lock: refill for whoever asks
}

void scanAdd(const MsgRec& m, uint32_t n, const Ptr& p) {
    uint8_t k = 0;
    for (; k < g_subjRows; ++k) if (g_subjRow[k].hash == m.hash) break;
    if (k == g_subjRows) {
        if (g_subjRows >= kMaxSubjects) return;
        g_subjRow[k] = SubjRow{};
        g_subjRow[k].hash   = m.hash;
        g_subjRow[k].newest = n;
        // The newest message's subject text is the one shown, so a
        // subject that was renamed displays as its latest form while
        // still grouping by the hash it has always had.
        snprintf(g_subjRow[k].subject, sizeof(g_subjRow[k].subject), "%s", m.subject);
        ++g_subjRows;
    }
    ++g_subjRow[k].total;
    // Walking newest to oldest, so the last write is the oldest message.
    g_subjRow[k].first = n;
    if (!seen(p, n)) ++g_subjRow[k].unread;
}

void scanDone() {
    uint32_t hi = 0;
    for (uint8_t k = 0; k < g_subjRows; ++k)
        if (g_subjRow[k].first > hi) hi = g_subjRow[k].first;
    g_subjNumW = 1;
    while (hi >= 10) { hi /= 10; ++g_subjNumW; }
}

// nextInSubject, nextUnread and liveUnread are walks now (1.1.2): see
// "The walks, a slice a pass" below. What they answered is unchanged:
//
// - the next message in a subject, read or not. Opening a subject must show
//   it even when there is nothing new in it (Rob posted the first message on
//   the board, which by definition he had read, then opened the subject and
//   got nothing): inside a conversation Enter means "what is next", at the
//   forum list it means "what is new".
// - the next message not read, forward from above the mark, oldest first,
//   honouring the subject filter.
// - what this caller has not read, removed messages not counted. Exact
//   arithmetic (unreadUpTo) while nothing was ever removed; only a forum
//   that has had a removal walks the records it has not seen.

// Removing a message is one byte: the live flag at kOffFlags goes from '.'
// to 'X'. A single byte cannot be half written, so there is no torn state to
// recover from, and nothing moves: the index is never compacted, every other
// message keeps its number, and every read pointer keeps meaning what it
// meant. The body stays in its segment file, so a removal is recoverable
// from the card by hand. The writer does it (runRemove, 1.2.1).

// ===========================================================================
// Drawing.
// ===========================================================================

uint8_t slotIdx(const Session& s) { return slotOf(s); }

// callerId: the account id behind this session, or 0 for a guest.
//
// A pointer is filed under the id and never the handle, which is the whole
// reason ids had to land before the forums did: a handle can be renamed and
// a renamed caller would otherwise come back to a board claiming everything
// was unread. 0 means "keep nothing", which is right for a guest: they have
// no account for a pointer to belong to.
//
// Looked up rather than cached on the Session because a rename, or a purge,
// during a call should take effect rather than be remembered wrongly until
// the caller hangs up.
uint32_t callerId(const Session& s) {
    if (s.guest || !s.user[0]) return 0;
    UserRec u;
    if (users::lookup(s.user, u) != users::Lookup::Found) return 0;
    return u.id;
}

// ===========================================================================
// The walks, a slice a pass (1.1.2).
//
// Every question the forums ask of the index that is not "message n" is a
// walk along it: the next message in a subject, the next one this caller has
// not read, the subjects of a forum, how many are unread once posts have
// been removed. They were loops on the loop: nextInSubject and nextUnread
// opened the index for every record (fopen, fseek, fread, fclose), with no
// cap, on every Enter, so a subject whose next message was a thousand
// records on cost seconds with the whole board waiting; scanSubjects read up
// to 2,000 records in one draw; liveUnread up to 2,000 for each of sixteen
// forums on the way in (internal/audit-1.1.2-2026-09-26.md, item 3).
//
// Now a walk is a small state per caller, advanced from the plugin's fast
// tick: the index opened once a slice, at most kSlice records read, and the
// place kept for the next slice. The common walk (the next message is the
// next record) finishes in its first slice, so Enter feels the same; a long
// one takes a few passes with a spinner on the prompt line, and every other
// caller carries on. What the walk was for (show the message, draw the
// list, jump) is its continuation, done when it finishes.
//
// On the loop, bounded by the slice, which is what Rule no. 1 asks. The
// writes are the runner's since 1.2.1 (the writer, above), and a walk reads
// only records up to `newest`, which moves on the loop once the runner has
// written them, so a walk and a write never want the same record.
//
// W_WRITE (1.2.1) is not a walk of the index at all: it is a caller waiting
// for the writer's answer, which borrows the walks' spinner, their key rule
// and their call check rather than growing a second copy of each. opDeliver
// finishes it.
// ===========================================================================

constexpr uint16_t kSlice     = 64;     // records read for one caller in one pass
constexpr uint16_t kTickBudget = 128;   // records read for everybody in one pass
constexpr uint32_t kSpinAfter = 250;    // ms before a walk shows it is working
constexpr uint16_t kPtrCost   = 16;     // a forum's read pointer loaded, in records
constexpr uint32_t kWriteGiveMs = 10000; // a write waited on this long: ESC may stop waiting

enum WalkKind : uint8_t {
    W_NONE = 0,
    W_SUBJECT,     // the next message in a subject, read or not
    W_UNREAD,      // the next message not read, in one forum (subject filter)
    W_ANY,         // the next message not read, in any forum this caller sees
    W_SCAN,        // the subjects of a forum, into the shared table
    W_COUNT,       // unread counts, removed posts not counted, for a set of forums
    W_WRITE,       // waiting for the writer (1.2.1): reads nothing
};

// What happens when the walk has its answer.
enum WalkThen : uint8_t {
    T_NONE = 0,
    T_SHOW,        // show the message found, or say there is nothing
    T_ENDSUBJ,     // after a subject ran out: the rest of the forum
    T_DRAWSUBJ,    // draw the subject list
    T_JUMP,        // a subject number was typed: open it
    T_DRAWFORUMS,  // the counts on the way in: draw the forum list
    T_POSTED,      // W_WRITE: a post's answer (found = its number, 0 not saved)
    T_REMOVED,     // W_WRITE: a removal's answer (found = the number, arg = RES_*)
};

struct Walk {
    uint8_t  kind  = W_NONE;
    uint8_t  then  = T_NONE;
    uint8_t  forum = 0xFF;        // the forum being walked now
    uint16_t mask  = 0;           // W_ANY, W_COUNT: forums still to walk
    uint32_t subject = 0;         // W_SUBJECT, W_UNREAD: the subject, 0 none; W_WRITE: the ticket
    uint32_t pos   = 0;           // the next record to look at
    uint32_t end   = 0;           // the last one (inclusive); W_SCAN counts down to it
    uint32_t acc   = 0;           // W_COUNT: unread so far in this forum
    uint32_t arg   = 0;           // T_JUMP: the number typed
    uint32_t found = 0;           // the answer: a message number, 0 none
    uint32_t startedAt = 0;
    uint32_t spinAt    = 0;
    uint8_t  spin      = 0;
    bool     fg        = false;   // the caller is waiting on it
    bool     shown     = false;   // the spinner is on the line
    uint16_t call      = 0;       // Session::call: the caller it is for
};
Walk g_walk[BBS_MAX_NODES + 2];
uint8_t g_walkNext = 0;           // round robin: whose slice comes first

// ---------------------------------------------------------------------------
// Read pointers, in RAM for the forum a caller is in (1.1.2). Written to the
// card when they leave the forum, the forums or the board, not at every
// message read: that was a card write per message on the reading path.
// ---------------------------------------------------------------------------
Ptr      g_ptr[BBS_MAX_NODES + 2];
uint8_t  g_ptrForum[BBS_MAX_NODES + 2];   // 0xFF: none loaded (set in start)
bool     g_ptrDirty[BBS_MAX_NODES + 2] = {};
uint32_t g_ptrUser[BBS_MAX_NODES + 2]  = {};
// The call it was loaded for (code review, 1.1.2): a caller who left the
// forums without leave() (a ring answered from inside) keeps their pointer
// in the slot, and the next caller on that node must not read from it.
uint16_t g_ptrCall[BBS_MAX_NODES + 2]  = {};

// ptrFlush: a changed pointer to the writer (1.2.1), which puts it on the
// card; readPtr finds it in the queue until it is there. A queue that will
// not take it loses the change: the caller is shown those messages as new
// again next time, the failure direction the pointer already accepts.
void ptrFlush(uint8_t sl) {
    if (g_ptrDirty[sl] && g_ptrForum[sl] != 0xFF && g_ptrUser[sl]) {
        Op op;
        op.kind  = OP_PTR;
        op.forum = g_ptrForum[sl];
        op.user  = g_ptrUser[sl];
        op.ptr   = g_ptr[sl];
        if (!opQueue(op)) plat::log("forums: a read pointer was not saved");
    }
    g_ptrDirty[sl] = false;
}

void ptrForget(uint8_t sl) {
    ptrFlush(sl);
    g_ptrForum[sl] = 0xFF;
    g_ptrUser[sl]  = 0;
}

// ptrFor: this caller's pointer for this forum, loaded from the card when it
// is not the one in RAM (the one that was is written first, if it changed).
Ptr& ptrFor(const Session& s, uint8_t forum) {
    uint8_t sl = slotOf(s);
    if (g_ptrForum[sl] != forum || g_ptrCall[sl] != s.call) {
        ptrFlush(sl);                          // the previous caller's, under their own id
        g_ptrUser[sl]  = callerId(s);
        g_ptrForum[sl] = forum;
        g_ptrCall[sl]  = s.call;
        readPtr(g_ptrUser[sl], forum, g_ptr[sl]);
    }
    return g_ptr[sl];
}

void ptrSeen(const Session& s, uint8_t forum, uint32_t n) {
    Ptr& p = ptrFor(s, forum);
    markSeen(p, n);
    g_ptrDirty[slotOf(s)] = true;
}

void walkResetAll() {
    memset(g_ptrForum, 0xFF, sizeof(g_ptrForum));
    memset(g_ptrDirty, 0, sizeof(g_ptrDirty));
    for (auto& w : g_walk) w = Walk();
}

void walkFlushAll() {
    for (uint8_t sl = 0; sl < BBS_MAX_NODES + 2; ++sl) { ptrFlush(sl); g_walk[sl] = Walk(); }
}

// ---------------------------------------------------------------------------
// Starting and stopping a walk.
// ---------------------------------------------------------------------------
void walkStop(uint8_t sl) {
    g_walk[sl] = Walk();
}

bool walking(uint8_t sl) { return g_walk[sl].kind != W_NONE; }

// firstForum: the lowest forum in a mask, 0xFF for none.
uint8_t firstForum(uint16_t mask) {
    for (uint8_t i = 0; i < kMaxForums; ++i) if (mask & (1u << i)) return i;
    return 0xFF;
}

// walkForum: aim the walk at the forum in hand, for its kind.
void walkAim(const Session& s, Walk& w) {
    const uint8_t f = w.forum;
    if (f == 0xFF) return;
    const Ptr& p = ptrFor(s, f);
    const uint32_t newest = g_forum[f].newest;
    switch (w.kind) {
        case W_ANY:
            w.pos = p.mark + 1;
            w.end = newest;
            break;
        case W_COUNT: {
            w.acc = 0;
            uint32_t n0 = p.mark + 1;
            if (newest > kScanMax && n0 < newest - kScanMax) n0 = newest - kScanMax;
            w.pos = n0;
            w.end = newest;
            break;
        }
        default:
            break;
    }
}

void walkBegin(Session& s, uint8_t kind, uint8_t then, uint8_t forum, bool fg) {
    uint8_t sl = slotOf(s);
    Walk& w = g_walk[sl];
    w = Walk();
    w.kind  = kind;
    w.then  = then;
    w.forum = forum;
    w.fg    = fg;
    w.call  = s.call;
    w.startedAt = plat::millis();
    w.spinAt    = w.startedAt + kSpinAfter;
}

// walkScan: the W_SCAN walk, the table reset for this caller as it starts.
void walkScanBegin(Session& s, uint8_t forum, uint8_t then) {
    walkBegin(s, W_SCAN, then, forum, true);
    Walk& w = g_walk[slotOf(s)];
    // Not from under another caller's fill: the slice takes the table once
    // theirs is done (see W_SCAN in walkSlice).
    const uint8_t o = claims::owner(claims::Res::Subjects);
    if (!(o != 0xFF && o != slotOf(s) && o < BBS_MAX_NODES + 2 && g_walk[o].kind == W_SCAN))
        scanReset(forum, slotOf(s));
    ptrFor(s, forum);
    w.pos = g_forum[forum].newest;
    w.end = w.pos > kScanMax ? w.pos - kScanMax : 1;
}

// ---------------------------------------------------------------------------
// One slice of a walk: up to `budget` records read, the index opened once.
// True when the walk has its answer.
// ---------------------------------------------------------------------------
bool walkSlice(Session& s, Walk& w, uint16_t& budget) {
    uint8_t sl = slotOf(s);
    if (w.kind == W_WRITE) return false;       // the writer's answer finishes it, not a slice
    for (;;) {
        // The forums walked one after another (W_ANY, W_COUNT).
        if (w.kind == W_ANY || w.kind == W_COUNT) {
            if (w.forum == 0xFF) {
                if (firstForum(w.mask) == 0xFF) return true;   // all walked
                // Another forum's read pointer is a read of the card too:
                // counted against the slice, so sixteen forums are not
                // sixteen opens in one pass.
                if (budget < kPtrCost) return false;
                budget = static_cast<uint16_t>(budget - kPtrCost);
                w.forum = firstForum(w.mask);
                w.mask = static_cast<uint16_t>(w.mask & ~(1u << w.forum));
                // Exact without reading when nothing was ever removed: the
                // header's live count is its highest number (liveUnread's rule).
                if (w.kind == W_COUNT && g_forum[w.forum].total >= g_forum[w.forum].newest) {
                    setUnread(sl, w.forum, unreadUpTo(ptrFor(s, w.forum), g_forum[w.forum].newest));
                    w.forum = 0xFF;
                    continue;
                }
                walkAim(s, w);
            }
        }
        if (!budget) return false;

        const uint8_t f = w.forum;
        const Ptr& p = ptrFor(s, f);
        char path[128];
        indexPath(f, path, sizeof(path));
        FILE* fh = nullptr;
        bool done = false;
        uint16_t read = 0;
        auto rec = [&](uint32_t n, MsgRec& m) -> bool {
            if (!fh) {
                fh = disk::open(path, "rb");
                if (!fh) return false;
            }
            char r[kRec];
            if (fseek(fh, static_cast<long>(n) * kRec, SEEK_SET) != 0 || fread(r, 1, kRec, fh) != kRec)
                return false;
            parseRec(r, m);
            ++read;
            return m.num == n;
        };

        switch (w.kind) {
            case W_SUBJECT:
            case W_UNREAD:
            case W_ANY: {
                const bool unread = w.kind != W_SUBJECT;
                while (w.pos <= w.end) {
                    if (unread && seen(p, w.pos)) { ++w.pos; continue; }    // no read: no cost
                    if (read >= budget || read >= kSlice) break;
                    MsgRec m;
                    const uint32_t n = w.pos++;
                    if (!rec(n, m) || !m.live) {
                        if (!fh) { w.pos = w.end + 1; break; }              // no index: nothing here
                        continue;
                    }
                    if (w.subject && m.hash != w.subject) continue;
                    w.found = n;
                    done = true;
                    break;
                }
                if (!done && w.pos > w.end) {
                    if (w.kind == W_ANY) { w.forum = 0xFF; if (fh) fclose(fh); budget = static_cast<uint16_t>(budget - read); continue; }
                    done = true;
                }
                break;
            }
            case W_COUNT: {
                while (w.pos <= w.end) {
                    if (seen(p, w.pos)) { ++w.pos; continue; }
                    if (read >= budget || read >= kSlice) break;
                    MsgRec m;
                    const uint32_t n = w.pos++;
                    if (rec(n, m) && m.live) ++w.acc;
                    else if (!fh) { w.pos = w.end + 1; break; }
                }
                if (w.pos > w.end) {
                    setUnread(sl, f, w.acc);
                    w.forum = 0xFF;
                    if (fh) fclose(fh);
                    budget = static_cast<uint16_t>(budget > read ? budget - read : 0);
                    continue;                                               // the next forum
                }
                break;
            }
            case W_SCAN: {
                // Backward from the newest, as scanSubjects always did: the
                // newest subjects are what a caller cares about, and stopping
                // early leaves them rather than the oldest. The table is the
                // board's one shared cache: if somebody else took it since
                // this scan began, it starts again for this caller.
                if (!claims::holds(claims::Res::Subjects, sl) || g_subjFor != f) {
                    // Another caller's fill still under way is waited for,
                    // not taken: two fills spread over passes took the table
                    // from each other every slice and neither ever ended
                    // (code review, 1.1.2). A cache is a lock while it fills.
                    const uint8_t o = claims::owner(claims::Res::Subjects);
                    if (o != 0xFF && o != sl && o < BBS_MAX_NODES + 2 && g_walk[o].kind == W_SCAN) break;
                    scanReset(f, sl);
                    w.pos = g_forum[f].newest;
                    w.end = w.pos > kScanMax ? w.pos - kScanMax : 1;
                }
                while (w.pos >= w.end && w.pos >= 1) {
                    if (read >= budget || read >= kSlice) break;
                    MsgRec m;
                    const uint32_t n = w.pos;
                    const bool ok = rec(n, m);
                    if (w.pos == 1) { w.pos = 0; }                           // uint32 would wrap below 1
                    else            { --w.pos; }
                    if (!ok) { if (!fh) { w.pos = 0; break; } continue; }
                    if (!m.live) continue;
                    scanAdd(m, n, p);
                }
                if (w.pos < w.end || w.pos == 0) { scanDone(); done = true; }
                break;
            }
            default:
                done = true;
                break;
        }
        if (fh) fclose(fh);
        budget = static_cast<uint16_t>(budget > read ? budget - read : 0);
        return done;
    }
}

// rows: one row per pass for whichever list this caller is looking at.
//
// Everything goes through startPluginList rather than being drawn directly.
// Sixteen forums at 132 columns is about 3,326 bytes of frame against a
// 3,072 byte timeline, and Term::ch returns void, so a frame that does not
// fit loses characters off the end with nothing able to report it.
bool readRow(Bbs& b, Session& s);      // a message, a row at a time (1.2.1), below

bool rows(Session& s) {
    Bbs& b = *g_bbs;
    uint8_t sl = slotIdx(s);
    uint8_t w  = b.rowWidth(s);

    if (g_view[sl] == View::Reading) return readRow(b, s);

    if (g_view[sl] == View::Subjects) {
        // Drawing somebody else's table would print their forum's subjects
        // under this caller's title bar.
        if (!claims::holds(claims::Res::Subjects, sl) || g_subjFor != g_at[sl]) return false;
        uint8_t row = s.listIdx;

        // The footer and the prompt are the last rows of the list, not
        // something drawn after it. A plugin list that runs to the end gets
        // no listDone (that fires only on an abort), so a list which does
        // not draw its own prompt leaves the caller looking at a bare
        // cursor with nothing saying what to press. That shipped.
        if (row == g_subjRows) {
            // Rule, then what Enter will do, then the footer and prompt.
            // prompt() prints the footer itself: this used to print one as
            // well, so it was on screen twice every time the list was drawn.
            g_bbs->rowRule(s);
            uint32_t here = 0;
            for (uint8_t x = 0; x < g_subjRows; ++x) here += g_subjRow[x].unread;
            listStatus(s, here, "Nothing new in this forum.");
            prompt(*g_bbs, s, false);
            // The prompt is the last row, and the pager counts it: when it
            // was the row that filled the page, the core drew [More] under
            // the prompt, on a list with nothing left to show (1.2.1, found
            // making the reader a list). Nothing follows it, so no page.
            s.nonstop = true;
            ++s.listIdx;
            return true;
        }
        if (row > g_subjRows) return false;
        const SubjRow& r = g_subjRow[row];

        // Clamped, because the compiler cannot see that a message number
        // is at most ten digits and warned that 255 would not fit.
        char num[12];
        uint8_t nw = g_subjNumW > 10 ? 10 : g_subjNumW;
        snprintf(num, sizeof(num), "%*lu", static_cast<int>(nw),
                 static_cast<unsigned long>(r.first));
        // Column 0 is the unread marker, not a margin (Rob: "not sure why
        // these all start indented, stop that").
        s.term.color(s.tl, Color::Yellow);
        s.term.ch(s.tl, r.unread ? '*' : ' ');
        s.term.text(s.tl, num);
        s.term.ch(s.tl, ' ');

        s.term.color(s.tl, r.unread ? Color::White : Color::Grey);
        uint8_t lead  = static_cast<uint8_t>(1 + strlen(num) + 1);
        uint8_t nameW = w > 56 ? 44 : static_cast<uint8_t>(w > 24 ? w - 18 : 6);
        uint8_t used  = static_cast<uint8_t>(lead + s.term.textCols(s.tl, r.subject, nameW));

        char tail[40];
        if (r.unread) snprintf(tail, sizeof(tail), "%lu of %lu new",
                               static_cast<unsigned long>(r.unread),
                               static_cast<unsigned long>(r.total));
        else          snprintf(tail, sizeof(tail), "%lu msg%s",
                               static_cast<unsigned long>(r.total),
                               r.total == 1 ? "" : "s");
        uint8_t tlen = static_cast<uint8_t>(strlen(tail));
        if (used + tlen + 2 <= w) {
            for (uint8_t k = used; k + tlen < w; ++k) s.term.ch(s.tl, ' ');
            s.term.color(s.tl, r.unread ? Color::LightGreen : Color::DarkGrey);
            s.term.text(s.tl, tail);
        }
        s.term.nl(s.tl);
        ++s.listIdx;
        return true;
    }

    // ---- the forum list -------------------------------------------------
    uint8_t vis[kMaxForums];
    uint8_t n = visibleForums(s, vis, kMaxForums);
    uint8_t row = s.listIdx;

    // The "what is new" line used to sit at the TOP of this list. Rob: it
    // belongs under the rule, with a blank line either side, which is where
    // the eye goes once it has read the list.
    uint8_t k = row;

    // The closing rule, the footer and the prompt are rows of this list, not
    // something drawn after it. listDone fires only on an abort, so a list
    // that runs to the end and draws no prompt leaves the caller looking at
    // a bare cursor: Rob saw exactly that, and had to press Enter to get a
    // prompt out of it.
    if (k == n) {
        uint32_t newTotal = 0;
        for (uint8_t x = 0; x < n; ++x) newTotal += g_unread[sl][vis[x]];
        g_bbs->rowRule(s);
        listStatus(s, newTotal, "Nothing new since your last call.");
        prompt(*g_bbs, s, false);
        s.nonstop = true;             // no [More] under the prompt: see the subject list's
        ++s.listIdx;
        return true;
    }
    if (k > n) return false;
    const Forum& f = g_forum[vis[k]];

    char num[6];
    snprintf(num, sizeof(num), "%2u", static_cast<unsigned>(k + 1));
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, num);
    s.term.ch(s.tl, ' ');

    s.term.color(s.tl, Color::White);
    uint8_t used = static_cast<uint8_t>(3 + s.term.textCols(s.tl, f.name, kNameMax));

    // The description earns its place only at 64 columns and up. At 40 the
    // width goes to the name and the count, because a truncated description
    // is worse than none; tin's `d` key toggled exactly this column.
    if (w >= 64 && f.about[0]) {
        for (uint8_t x = used; x < kNameMax + 4; ++x) { s.term.ch(s.tl, ' '); ++used; }
        s.term.color(s.tl, Color::Grey);
        used = static_cast<uint8_t>(used + s.term.textCols(s.tl, f.about, 34));
    }

    // Zero shows as nothing, and the row is grey. A column of noughts reads
    // as a fault rather than as quiet, which is why Citadel printed its
    // arrival count only when it was non-zero.
    char cnt[20] = "";
    uint32_t fu = g_unread[sl][vis[k]];
    if (fu) snprintf(cnt, sizeof(cnt), "%lu new", static_cast<unsigned long>(fu));
    uint8_t clen = static_cast<uint8_t>(strlen(cnt));
    if (clen && used + clen + 2 <= w) {
        for (uint8_t x = used; x + clen < w; ++x) s.term.ch(s.tl, ' ');
        s.term.color(s.tl, Color::LightGreen);
        s.term.text(s.tl, cnt);
    }
    s.term.nl(s.tl);
    ++s.listIdx;
    return true;
}

// say: one system line, wrapped at the READER's width.
//
// These lines used to go out as single literals up to 67 characters wide,
// which is fine on SyncTERM and wraps in the terminal on a C64. Shortening
// them all to 39 would have fixed the C64 by making every wide terminal
// worse, which is the habit the rowWidth rework already went through once.
//
// A leading "--> " marker or run of spaces is furniture rather than text: it
// is printed on the first line, and continuations are indented to line up
// under the words instead of under the arrow. Without that a wrapped notice
// reads as two unrelated lines.
void say(Session& s, Color c, const char* text) {
    if (!text || !*text) return;

    uint8_t ind = 0;
    while (text[ind] == ' ' && ind < 8) ++ind;          // a deliberate indent
    // The marker counts as furniture after a margin too, so " --> text"
    // wraps with its continuation lined up under "text".
    if (text[ind] == '-' && text[ind + 1] == '-' && text[ind + 2] == '>') {
        ind = static_cast<uint8_t>(ind + 3);
        while (text[ind] == ' ' && ind < 12) ++ind;
    }

    uint8_t cols = Bbs::instance().rowWidth(s);
    uint8_t body = (cols > static_cast<uint8_t>(ind + 8))
                 ? static_cast<uint8_t>(cols - ind)
                 : cols;

    char line[160];
    const char* rest  = text + ind;
    uint8_t     guard = 0;
    bool        first = true;
    while ((rest = bbsu::wrap(rest, line, sizeof(line), body)) != nullptr
           && ++guard < 12) {
        s.term.color(s.tl, c);
        if (first) {
            for (uint8_t i = 0; i < ind; ++i) s.term.ch(s.tl, text[i]);
        } else {
            for (uint8_t i = 0; i < ind; ++i) s.term.ch(s.tl, ' ');
        }
        s.term.text(s.tl, line);
        first = false;
        if (!*rest) break;
        s.term.nl(s.tl);
    }
}

// notice: the board answering a key pressed at the prompt.
//
// Rob, three times in one sitting: "Linefeed before --> That is the end",
// "Linefeed before --> Nothing new", and the same for the message header.
// An answer printed on the line straight under the prompt reads as part of
// the prompt. The first newline ends the prompt line the key was pressed
// on, the second sets the answer off from it. Column 0, like every other
// line the board says (Rob: "--> starts at the very begining. EVERYWHERE").
//
// Only for a SINGLE KEY at the prompt, which prints no newline of its own.
// A line finished with Enter in an editor has already moved down, so a
// notice after one needs a single newline, not this.
void notice(Session& s, Color c, const char* text) {
    s.term.nl(s.tl);
    s.term.nl(s.tl);
    say(s, c, text);
}

// listStatus: what Enter will do, under a list's closing rule.
//
// Rob's wording and placement: under the rule, a blank line above and
// below, "N new messages are ready to read. [Enter] to start reading
// unread." when there is something and `none` when there is not. One line
// or two as the width needs, never a cut one.
void listStatus(Session& s, uint32_t unread, const char* none) {
    char line[128];
    if (unread)
        snprintf(line, sizeof(line),
                 "%lu new message%s ready to read. [Enter] to start reading unread.",
                 static_cast<unsigned long>(unread), unread == 1 ? " is" : "s are");
    else
        snprintf(line, sizeof(line), "%s", none);
    s.term.nl(s.tl);
    say(s, unread ? Color::LightGreen : Color::Grey, line);
    s.term.nl(s.tl);
    s.term.nl(s.tl);
}

// readPrompt: the question under a message, asked the way mail asks it.
//
// Rob: "When reading, ask like email, reply, enter for next, etc. the way
// you have it is not intuitive while reading. The prompt is fine elsewhere,
// just not when directly reading a post." A footer and a breadcrumb suit a
// list, where the caller is choosing where to go. Under a message the
// question is what to do with what was just read, and that wants the verbs,
// the way mail's [R]eply [S]ave [D]elete does, in the same colours. The
// keys not named here (# to jump, ? for help) still work, and ? lists them.
// noticeDone: the prompt after a notice. In the reading view the notice
// is followed straight by the reading prompt, and say() leaves the cursor
// at the end of the notice's line, so prompt()'s one newline put the two
// on consecutive lines (Rob, 0.22.2: "Need LF after that message"). EOM
// ends its own line, which is why the same prompt() left a gap there and
// not here. The list views end with a footer that already stands apart.
void noticeDone(Bbs& b, Session& s) {
    if (g_view[slotIdx(s)] == View::Reading) s.term.nl(s.tl);
    prompt(b, s);
}

void readPrompt(Bbs& b, Session& s) {
    uint8_t sl  = slotIdx(s);
    bool    mod = mayMod(s, g_at[sl]);
    // 51 columns at the widest, so the long form needs a wide terminal and
    // a C64 gets the short one, which drops P for a moderator to make room
    // for D. P still works; ? says so.
    const char* q = b.rowWidth(s) >= 59
        ? (mod ? "[R]eply  [Enter] Next  [P]ost  [D]elete  [Q] Back: "
               : "[R]eply  [Enter] Next  [P]ost  [Q] Back: ")
        : (mod ? "[R]eply [D]el [Enter]Next [Q]Back: "
               : "[R]eply [Enter]Next [P]ost [Q]Back: ");
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, q);
    s.term.color(s.tl, Color::White);
}

void prompt(Bbs& b, Session& s, bool gap) {
    uint8_t sl = slotIdx(s);
    if (gap) s.term.nl(s.tl);
    if (g_view[sl] == View::Reading) {
        readPrompt(b, s);
        return;
    }
    // Both through say(), which sets its own colour. The second one was a
    // bare text() and inherited whatever came before it, so after an
    // end-of-subject notice the footer came out cyan.
    if (g_view[sl] == View::Forums)
        say(s, Color::Grey, "--> Enter reads what is new. # - Open a forum. ? help. Q leaves.");
    else
        say(s, Color::Grey, "--> Enter reads on. # - Jump to subject. P posts. ? help. Q back.");
    s.term.nl(s.tl);
    s.term.nl(s.tl);          // Rob: a blank line before the prompt

    // The breadcrumb (UX spec F2). Three levels, fixed words at both ends
    // and the place in the middle, so a caller always knows which of the
    // three they are standing on:
    //
    //   Forums>                 the forum list
    //   Forums>C64>             the subject list, and reading
    //
    // There was a third level, Forums>C64>Messages>, for reading. Rob: "why
    // do we need messages, we're in the forum topic, messages is
    // superfluous". The header over each message already says it is one.
    //
    // No number in it, deliberately: a message is #412 and a subject is 12,
    // and a prompt carrying one of them next to the other in the post rule
    // is a collision waiting to happen. Numbers belong in lists and headers,
    // beside the thing they name. The old form was "[F1] <name}> ", which
    // put a forum number in the prompt and never said what level it was.
    //
    // Every input inside FORUMS is a single keypress, so a long prompt costs
    // no typing room. The same prompt at the main shell would not be
    // affordable.
    s.term.color(s.tl, g_cTitle);
    s.term.text(s.tl, "Forums>");
    if (g_view[sl] != View::Forums) {
        // Cut to rowWidth - 17, which is 22 at 40 columns against a 24
        // character maximum, so it is cut only for the two longest possible
        // names and never at 80 or above.
        uint8_t w = b.rowWidth(s);
        uint8_t nameW = w > 17 ? static_cast<uint8_t>(w - 17) : 8;
        s.term.color(s.tl, g_cSubj);
        s.term.textCols(s.tl, g_forum[g_at[sl]].name, nameW);
        s.term.color(s.tl, g_cTitle);
        s.term.text(s.tl, ">");
    }
    s.term.ch(s.tl, ' ');
}

void drawForums(Bbs& b, Session& s) {
    uint8_t sl = slotIdx(s);
    g_view[sl] = View::Forums;
    g_at[sl]   = 0xFF;
    g_subj[sl] = 0;

    char right[24];
    uint8_t vis[kMaxForums];
    uint8_t n = visibleForums(s, vis, kMaxForums);
    uint32_t newTotal = 0;
    for (uint8_t k = 0; k < n; ++k) newTotal += g_unread[sl][vis[k]];
    // One form for every count, zero included (Rob): "0 new messages" rather
    // than a lower-case "nothing new" that read like a fragment. Singular
    // only for exactly one.
    snprintf(right, sizeof(right), "%lu new message%s",
             static_cast<unsigned long>(newTotal), newTotal == 1 ? "" : "s");

    s.term.cls(s.tl);
    b.rowTitle(s, "Forums", right);
    s.term.nl(s.tl);          // Rob: a line between the header and the list
    s.listIdx = 0;
    b.startPluginList(s, g_index);
}

void walkRun(Bbs& b, Session& s);

void drawSubjects(Bbs& b, Session& s, uint8_t forum) {
    uint8_t sl = slotIdx(s);
    g_view[sl] = View::Subjects;
    g_at[sl]   = forum;
    g_subj[sl] = 0;
    // The table is a walk now (1.1.2): drawn when it is filled, straight
    // away for a forum that fits one slice.
    walkScanBegin(s, forum, T_DRAWSUBJ);
    walkRun(b, s);
}

// drawSubjectsNow: the list, once the scan walk has filled the table.
void drawSubjectsNow(Bbs& b, Session& s) {
    uint8_t sl = slotIdx(s);
    const uint8_t forum = g_at[sl];

    char right[24];
    uint32_t unread = 0;
    for (uint8_t k = 0; k < g_subjRows; ++k) unread += g_subjRow[k].unread;
    // The same form as the forum list's title, every count including zero.
    snprintf(right, sizeof(right), "%lu new message%s",
             static_cast<unsigned long>(unread), unread == 1 ? "" : "s");

    s.term.cls(s.tl);
    b.rowTitle(s, g_forum[forum].name, right);
    s.term.nl(s.tl);          // Rob: a line between the header and the list

    if (!g_subjRows) {
        b.rowRule(s);
        listStatus(s, 0, "Nothing here yet. P starts the first subject.");
        prompt(b, s, false);
        return;
    }
    s.listIdx = 0;
    b.startPluginList(s, g_index);
}

// headRule: "== New Message: ID #4 ----------" out to the width.
//
// Rob kept the rule ("#4 row is fine") and wanted the right words in it.
// "New Message" when this caller has not read it, "Message" when they have,
// because a label that says New on something already read is a small lie
// the eye learns to skip.
void headRule(Bbs& b, Session& s, bool fresh, uint32_t num) {
    Term& t = s.term;
    Timeline& tl = s.tl;
    uint8_t w = b.rowWidth(s);
    const char* label = fresh ? "New Message: ID " : "Message: ID ";
    char id[16];
    snprintf(id, sizeof(id), "#%lu", static_cast<unsigned long>(num));

    t.color(tl, g_cTitle);
    t.glyphs(tl, Glyph::HLine2, 2);
    t.ch(tl, ' ');
    t.color(tl, g_cSubj);
    t.text(tl, label);
    t.color(tl, g_cCount);
    t.text(tl, id);
    t.ch(tl, ' ');
    size_t used = 2 + 1 + strlen(label) + strlen(id) + 1;
    if (used < w) {
        t.color(tl, g_cTitle);
        t.glyphs(tl, Glyph::HLine, static_cast<uint8_t>(w - used));
    }
    t.nl(tl);
}

// plainRule: the rule between the header and the body.
void plainRule(Bbs& b, Session& s) {
    uint8_t w = b.rowWidth(s);
    s.term.color(s.tl, g_cTitle);
    s.term.glyphs(s.tl, Glyph::HLine, w ? w : 1);
    s.term.nl(s.tl);
}

// field: one labelled header line. The labels are the rules' colour, so the
// header reads as one block with them; the values are aligned under each
// other, which is what makes three lines read as a header and not a list.
void field(Bbs& b, Session& s, const char* label, Color c, const char* value) {
    uint8_t w = b.rowWidth(s);
    size_t lead = strlen(label);
    s.term.color(s.tl, g_cTitle);
    s.term.text(s.tl, label);
    s.term.color(s.tl, c);
    s.term.textCols(s.tl, value, static_cast<uint8_t>(w > lead ? w - lead : 1));
    s.term.nl(s.tl);
}

// ---------------------------------------------------------------------------
// showMessage: one message, and the pointer moved to say it was shown.
//
// **The screen is not cleared between messages.** Entering the subsystem
// clears, a listing clears, help clears. The reading loop does not, because
// it is a scroll and not a view: the previous message is the context for
// this one and somebody glancing back at what they just read should be able
// to. This is the one place the "screen clears" rule from the file manager
// deliberately does not apply, and it needs saying out loud because somebody
// will try to make it consistent.
//
// **A row at a time since 1.2.1**, through the core's list machinery like
// every other list here. It was drawn whole in one pass into the caller's
// 3 KB output buffer, from a 1,729-byte buffer on the BBS task's stack, so
// no body over 1,728 bytes could be shown and one near that on a slow
// terminal could overrun the buffer. Now the header, each wrapped line of
// the body, the EOM and the question are rows: drawn while the buffer has
// room, so a slow terminal is paced rather than overrun and every other
// caller carries on, and paged at [More] like every other list, which a
// long message on a 25-row screen needs. The body is read from the card a
// window at a time into the caller's compose buffer (free while they read:
// nobody writes a message and reads one at once, the information pages'
// argument), so any length the format can say is shown, and it costs no
// per-caller state: the reader's place lives at the head of that buffer.
// ---------------------------------------------------------------------------
enum : uint8_t { RD_TOP = 0, RD_TOP2, RD_RULE, RD_SUBJ, RD_BY, RD_DATE, RD_RULE2, RD_BODY,
                 RD_GAP, RD_EOM, RD_GAP2, RD_PROMPT, RD_END };

struct Reader {
    uint8_t  phase = RD_TOP;
    bool     fresh = false;        // new to this caller: "New Message" in the rule
    bool     bad   = false;        // the card would not give the rest of the body
    uint8_t  forum = 0xFF;
    uint16_t seg   = 0;
    uint16_t len   = 0;            // body bytes, at most kBodyCap
    uint16_t rd    = 0;            // body bytes drawn
    uint16_t wOfs  = 0;            // the body offset the window starts at
    uint16_t wLen  = 0;            // body bytes in the window
    uint32_t ofs   = 0;            // the body's offset in its segment
    uint32_t num   = 0;
    uint32_t epoch = 0;
    codes::Painter pt;             // colour and code count carry across rows
    char     handle[kHandle + 1]   = {};
    char     subject[kSubject + 1] = {};
};
static_assert(std::is_trivially_copyable<Reader>::value, "the reader is copied in and out of compose");

constexpr size_t   kReaderAt = (sizeof(Reader) + 7) & ~static_cast<size_t>(7);
constexpr size_t   kWindow   = sizeof(Session::compose) - kReaderAt - 1;   // the NUL after it
// Body bytes kept ahead of the one being drawn. A wrapped row takes at most
// the row buffer (160) of text and codes, so a window that holds this much
// past the place always holds the whole of the next row; it is refilled
// from the place when it does not.
constexpr uint16_t kLook     = 512;
static_assert(kWindow >= 2u * kLook, "the window holds a row ahead with room to spare");

void readerGet(const Session& s, Reader& r) { memcpy(&r, s.compose, sizeof(r)); }
void readerPut(Session& s, const Reader& r) { memcpy(s.compose, &r, sizeof(r)); }

void showMessage(Bbs& b, Session& s, uint8_t forum, uint32_t n) {
    MsgRec m;
    if (!readRec(forum, n, m)) {
        notice(s, g_cMark, "--> That message is not on the card.");
        noticeDone(b, s);
        return;
    }

    uint8_t sl = slotIdx(s);
    g_shown[sl] = n;
    g_view[sl]  = View::Reading;
    g_at[sl]    = forum;

    Reader r;
    // Read the pointer BEFORE marking, so the header can say whether this
    // was new to this caller. The one in RAM (1.1.2).
    r.fresh = !seen(ptrFor(s, forum), n);
    r.forum = forum;
    r.num   = m.num;
    r.epoch = m.epoch;
    r.seg   = m.seg;
    r.ofs   = m.ofs;
    r.len   = m.len > kBodyCap ? kBodyCap : m.len;
    snprintf(r.handle, sizeof(r.handle), "%s", m.handle);
    snprintf(r.subject, sizeof(r.subject), "%s", m.subject);
    r.pt.begin(g_cBody, !s.bellOff);
    readerPut(s, r);

    // Marked when it opens, in RAM, written to the card when the caller
    // leaves the forum (1.1.2). A caller who stops a long one at [More] has
    // still been shown it.
    ptrSeen(s, forum, n);

    // The forum's unread count follows what this caller has actually read,
    // so the list they come back to agrees with what just happened.
    unreadNow(s, forum);

    s.listIdx = 0;
    b.startPluginList(s, g_index);
}

// readerFill: the window, from the place, when the next row may not be in
// it. One open of the segment, at most kWindow bytes. A short read keeps
// what came and ends the body there, said as such.
void readerFill(Session& s, Reader& r) {
    const uint32_t have = static_cast<uint32_t>(r.wOfs) + r.wLen;
    const uint32_t need = static_cast<uint32_t>(r.rd) + kLook;
    if (r.rd >= r.wOfs && (have >= r.len || need <= have)) return;
    char* win = s.compose + kReaderAt;
    const size_t left = static_cast<size_t>(r.len - r.rd);
    const size_t want = left < kWindow ? left : kWindow;
    char dir[96], path[128];
    forumDir(r.forum, dir, sizeof(dir));
    segPath(dir, r.seg, path, sizeof(path));
    size_t got = 0;
    if (FILE* f = disk::open(path, "rb")) {
        if (fseek(f, static_cast<long>(r.ofs + r.rd), SEEK_SET) == 0) got = fread(win, 1, want, f);
        fclose(f);
    }
    win[got] = '\0';
    r.wOfs = r.rd;
    r.wLen = static_cast<uint16_t>(got);
    if (got < want) {
        r.bad = true;
        r.len = static_cast<uint16_t>(r.rd + got);   // what there is, and no more
    }
}

// bodyRow: one wrapped row of the body. False when the body is done.
bool bodyRow(Bbs& b, Session& s, Reader& r) {
    if (r.rd >= r.len) return false;
    readerFill(s, r);
    const char* win = s.compose + kReaderAt;
    const char* p   = win + (r.rd - r.wOfs);
    if (p >= win + r.wLen || !*p) return false;     // the end, or a NUL in the text
    // Wrapped at THIS reader's width, not the writer's, through the @-code
    // renderer, whose wrap measures what is shown rather than what was
    // typed. A colour lasts to the end of the writer's own line, across
    // however many rows this reader's width turns it into, and not into the
    // next paragraph.
    uint8_t w = b.rowWidth(s);
    if (!w) w = 1;
    char line[160];
    // The reserve is what an effect must leave for the rest of this row: the
    // rows after it are drawn in later passes, each only once the buffer has
    // room, so the whole body no longer has to fit behind the effect.
    r.pt.reserve = 384;
    const char* next = codes::wrap(p, line, sizeof(line), w, r.pt);
    if (!next || next <= p) return false;
    codes::row(s.term, s.tl, line, w, r.pt);
    s.term.nl(s.tl);
    if (next[-1] == '\n') codes::endParagraph(r.pt);
    r.rd = static_cast<uint16_t>(r.rd + (next - p));
    return true;
}

// readRow: the reading view's rows() (1.2.1). One row a call, so the pager
// counts the screen right.
bool readRow(Bbs& b, Session& s) {
    Reader r;
    readerGet(s, r);
    bool drew = true;
    for (bool again = true; again; ) {
        again = false;
        switch (r.phase) {
            case RD_TOP:
                // Rob's layout:
                //
                //   == New Message: ID #4 ---------------
                //   Subject: Wrapping test
                //   By:      quantumrob
                //   Date:    22 Sep 12:32
                //   -------------------------------------
                //   the body
                //
                //   --> EOM <--
                //
                //   [R]eply  [Enter] Next  [P]ost  [Q] Back:
                //
                // All of it from column 0 (Rob: "not sure why these all start
                // indented, stop that"). Two newlines first: one ends the
                // prompt line the key was pressed on, the second is the blank
                // line he asked for above the header. A row each: one line
                // a row is what keeps the pager's count true.
                s.term.nl(s.tl);
                r.phase = RD_TOP2;
                break;
            case RD_TOP2:
                s.term.nl(s.tl);
                r.phase = RD_RULE;
                break;
            case RD_RULE:
                headRule(b, s, r.fresh, r.num);
                r.phase = RD_SUBJ;
                break;
            case RD_SUBJ:
                field(b, s, "Subject: ", g_cHead, r.subject);
                r.phase = RD_BY;
                break;
            case RD_BY:
                field(b, s, "By:      ", g_cWho, r.handle);
                r.phase = RD_DATE;
                break;
            case RD_DATE: {
                char when[24] = "";
                clk::fmtEpoch(when, sizeof(when), "%d %b %H:%M", r.epoch);
                field(b, s, "Date:    ", g_cWhen, when);
                r.phase = RD_RULE2;
                break;
            }
            case RD_RULE2:
                plainRule(b, s);
                r.phase = RD_BODY;
                break;
            case RD_BODY:
                if (bodyRow(b, s, r)) break;
                if (r.bad) {
                    say(s, Color::LightRed, r.rd ? "(the rest of this message could not be read)"
                                                 : "(the text of this message could not be read)");
                    s.term.nl(s.tl);
                    r.phase = RD_EOM;              // the note stands for the blank line
                    break;
                }
                r.phase = RD_GAP;
                again = true;                          // nothing drawn: the gap in this row
                break;
            // Rob: "at the end of messages (all) add --> EOM <--". The reading
            // loop does not clear between messages, so a marker at the end of
            // each one is what says where one stops and the next begins. A
            // blank line before it (Rob: "Need a linefeed before EOM"), and one
            // before the question. **One line a row** (code review,
            // 1.2.1-forums.3): the pager counts rows, and the EOM row drew two
            // lines, so a page could hold one line more than the screen and
            // scroll an unread one off the top.
            case RD_GAP: {
                // The ending (blank, EOM, blank, then the question on the line
                // a [More] would take) stays on one page: with fewer than three
                // rows left on this one, the page ends here and the ending
                // opens the next, so a [More] never stops with only the ending
                // behind it. The page size is the core's (Bbs::pageRows, which
                // is private, so its rule is repeated here).
                const uint8_t tr = s.term.rows();
                const uint8_t page = static_cast<uint8_t>(tr > 8 ? tr - 2 : 6);
                if (!s.nonstop && s.pageLines + 3 > page) {
                    s.pageLines = page;                // the pager's [More], before the ending
                    break;                             // counted, drawn nothing, same phase next
                }
                s.term.nl(s.tl);
                r.phase = RD_EOM;
                break;
            }
            case RD_EOM:
                s.term.color(s.tl, g_cTitle);
                s.term.text(s.tl, "--> EOM <--");
                s.term.nl(s.tl);
                r.phase = RD_GAP2;
                break;
            case RD_GAP2:
                s.term.nl(s.tl);
                // No page break before the question: it has no newline of
                // its own, so it takes the line a [More] would have, and a
                // [More] with only the question behind it stops for nothing.
                s.nonstop = true;
                r.phase = RD_PROMPT;
                break;
            case RD_PROMPT:
                prompt(b, s, false);
                r.phase = RD_END;
                break;

            default:
                drew = false;
                break;
        }
    }
    readerPut(s, r);
    if (drew) ++s.listIdx;
    return drew;
}

// readNext: Enter. The whole fast path, as walks (1.1.2); the answer is
// acted on in walkFinish.
void readNext(Bbs& b, Session& s) {
    uint8_t sl = slotIdx(s);
    uint8_t forum = g_at[sl];

    // From the forum list, Enter walks INTO the first forum with something
    // unread rather than being a second mode. One mechanism, not two code
    // paths drawing the same message header and drifting apart.
    if (g_view[sl] == View::Forums || forum == 0xFF) {
        uint8_t vis[kMaxForums];
        uint8_t n = visibleForums(s, vis, kMaxForums);
        uint16_t mask = 0;
        for (uint8_t k = 0; k < n; ++k) mask = static_cast<uint16_t>(mask | (1u << vis[k]));
        walkBegin(s, W_ANY, T_SHOW, 0xFF, true);
        g_walk[sl].mask = mask;
        walkRun(b, s);
        return;
    }

    uint32_t from = (g_view[sl] == View::Reading) ? g_shown[sl] : 0;
    // Inside a conversation, walk it in order whether or not it has been
    // read. Only the forum-level Enter is about what is new.
    if (g_subj[sl]) {
        walkBegin(s, W_SUBJECT, T_SHOW, forum, true);
        g_walk[sl].subject = g_subj[sl];
        g_walk[sl].pos     = from + 1;
        g_walk[sl].end     = g_forum[forum].newest;
        walkRun(b, s);
        return;
    }
    walkBegin(s, W_UNREAD, T_SHOW, forum, true);
    g_walk[sl].pos = from ? from + 1 : ptrFor(s, forum).mark + 1;
    g_walk[sl].end = g_forum[forum].newest;
    walkRun(b, s);
}

// ===========================================================================
// Posting.
// ===========================================================================

char     g_draftSubject[BBS_MAX_NODES + 2][kSubject + 1] = {};
bool     g_replying[BBS_MAX_NODES + 2] = {};
uint32_t g_replyHash[BBS_MAX_NODES + 2] = {};

// What a typed line will be taken as when it arrives. The plugin owns the
// session, so it drives the ordinary line editor and reads the result back
// on Enter, which is how the caller keeps backspace and every terminal
// behaviour they already know.
enum : uint8_t { AskNone = 0, AskSubject, AskBody, AskJump, AskRemove };
uint8_t g_ask[BBS_MAX_NODES + 2] = {};

// ---------------------------------------------------------------------------
// The body, gathered a line at a time.
//
// **It cannot use s.ed for the whole message and that is not a tuning
// problem.** LineEditor holds `char buf_[BBS_LINE_MAX + 1]`, 73 bytes, and
// `begin()` takes a uint8_t. Asking it for a 1,728 byte body did not fail or
// warn: it gave back 72 characters and the caller found out by running out
// of room mid-sentence. FF_TEXTAREA is no better, being four 37 column rows.
//
// So the body is collected the way every real board collected one: line by
// line, each through the ordinary editor, ended with a command on its own
// line. That is not nostalgia. It gives the caller backspace and every
// terminal behaviour they already know on each line, it works identically at
// 40 and 132 columns, and it needs no cursor addressing, so it behaves the
// same on a C64 and a VT220.
// ---------------------------------------------------------------------------
// The body lives on the Session, shared with mail and with whatever takes a
// message next. Only the lengths are per-slot here, which is a few bytes.
constexpr uint8_t kBodyLines = BBS_COMPOSE_ROWS;

uint16_t g_bodyLen[BBS_MAX_NODES + 2]  = {};
uint8_t  g_bodyRows[BBS_MAX_NODES + 2] = {};

void askLine(Session& s, uint8_t what, const char* q, uint16_t cap) {
    g_ask[slotOf(s)] = what;
    s.term.color(s.tl, g_cAsk);
    s.term.text(s.tl, q);
    s.term.color(s.tl, g_cBody);
    // The editor's real capacity, never a number larger than it can hold.
    // A cap it silently clamps is how the 72 character body happened.
    uint8_t room = cap > BBS_LINE_MAX ? BBS_LINE_MAX : static_cast<uint8_t>(cap);
    s.ed.begin(room, 0);
}

// bodyWidth: how wide a body line is for THIS caller.
//
// The editor capacity and the wrap trigger must be the same number or the
// board wraps at a width the editor cannot hold, so it is computed once
// here and never written out as a constant. kBodyPromptCols is the "16: "
// that bodyPrompt draws, and the rub-out loops below count the same four.
constexpr uint8_t kBodyPromptCols = 4;
uint8_t bodyWidth(Session& s) {
    return compose::lineWidth(s.term.cols(), kBodyPromptCols, BBS_LINE_MAX);
}

// bodyPrompt: the line number, so a caller can see how much room is left.
// keep: after a notice (1.1.0), draw it again over what is being typed.
void bodyPrompt(Session& s, bool keep = false) {
    uint8_t sl = slotOf(s);
    char q[12];
    snprintf(q, sizeof(q), "%2u: ", static_cast<unsigned>(g_bodyRows[sl] + 1));
    g_ask[sl] = AskBody;
    s.term.color(s.tl, g_cMeta);
    s.term.text(s.tl, q);
    s.term.color(s.tl, g_cBody);
    if (keep && s.ed.active()) { s.ed.redraw(s.term, s.tl); return; }
    // The line follows the terminal, not a constant. Four columns for
    // the "16: " the prompt just wrote.
    s.ed.begin(bodyWidth(s), 0);
}

// bodyStart: clear the screen, say what is being written, and open the editor.
//
// Without the clear, the subject prompt appeared under whatever was already
// on screen, which on the way in from `?` is the help text. A caller cannot
// tell a prompt from the wreckage of the last screen.
void bodyBegin(Bbs& b, Session& s, const char* forumName, const char* subject) {
    uint8_t sl = slotOf(s);
    s.compose[0] = '\0';
    g_bodyLen[sl]  = 0;
    g_bodyRows[sl] = 0;

    s.term.cls(s.tl);
    char bar[80];
    snprintf(bar, sizeof(bar), "Posting in %.30s", forumName);
    b.rowBar(s, g_cAsk, bar);

    s.term.nl(s.tl);
    s.term.color(s.tl, g_cMeta);
    s.term.text(s.tl, "Subject: ");
    s.term.color(s.tl, g_cHead);
    s.term.textCols(s.tl, subject, b.rowWidth(s));
    s.term.nl(s.tl);
    s.term.nl(s.tl);

    s.term.color(s.tl, g_cMeta);
    char how[140];
    snprintf(how, sizeof(how),
             "Up to %u lines, %u characters. Long lines wrap by themselves.",
             static_cast<unsigned>(kBodyLines),
             static_cast<unsigned>(BBS_COMPOSE_MAX));
    s.term.text(s.tl, how);
    s.term.nl(s.tl);
    // The way out, in its own colour, because a caller who cannot find it is
    // stuck inside the editor with no way forward.
    s.term.color(s.tl, g_cAsk);
    s.term.text(s.tl, "/s");
    s.term.color(s.tl, g_cMeta);
    s.term.text(s.tl, compose::kHowToEnd);
    s.term.color(s.tl, g_cAsk);
    s.term.text(s.tl, "/a");
    s.term.color(s.tl, g_cMeta);
    s.term.text(s.tl, compose::kHowToDrop);
    s.term.nl(s.tl);
    b.rowRule(s);
    bodyPrompt(s);
}

// bodyAdd: one finished line. Returns false when there is no room left.
bool bodyAdd(Session& s, const char* line) {
    uint8_t sl = slotOf(s);
    size_t n = strlen(line);
    if (g_bodyLen[sl] + n + 1 >= BBS_COMPOSE_MAX) return false;
    if (g_bodyLen[sl]) s.compose[g_bodyLen[sl]++] = '\n';
    memcpy(s.compose + g_bodyLen[sl], line, n);
    g_bodyLen[sl] = static_cast<uint16_t>(g_bodyLen[sl] + n);
    s.compose[g_bodyLen[sl]] = '\0';
    ++g_bodyRows[sl];
    return true;
}

// finishPost: /s. The post goes to the writer (1.2.1) and the caller waits
// for its answer (W_WRITE, then T_POSTED in walkFinish): "Posted as message
// N" is said once it is on the card and the forum's figures say so, never
// before, so the poster can read it the moment they are told.
void finishPost(Bbs& b, Session& s, const char* body) {
    (void)b;
    uint8_t sl = slotIdx(s);
    uint8_t forum = g_at[sl];

    size_t len = strlen(body);
    if (len > kBodyCap) len = kBodyCap;           // never past the format's four digits

    // The text is copied: the writer must never read a Session, and this
    // caller's compose buffer is the next caller's if they hang up now.
    PostData* pd = static_cast<PostData*>(opAlloc(sizeof(PostData) + len + 1));
    if (pd) {
        new (pd) PostData();
        MsgRec& m = pd->m;
        m.authorId = callerId(s);
        snprintf(m.handle, sizeof(m.handle), "%.*s", kHandle, s.user);
        m.epoch = clk::epoch();
        snprintf(m.subject, sizeof(m.subject), "%.*s", kSubject, g_draftSubject[sl]);
        // A reply carries its parent's hash rather than rehashing the text, so
        // renaming a subject cannot split a thread and two subjects that happen
        // to read alike cannot be merged into one.
        m.hash = g_replying[sl] ? g_replyHash[sl] : subjectHash(m.subject);
        pd->len = static_cast<uint16_t>(len);
        memcpy(pd->body(), body, len);
        pd->body()[len] = '\0';
    }
    Op op;
    op.kind   = OP_POST;
    op.forum  = forum;
    op.node   = sl;
    op.call   = s.call;
    op.ticket = nextTicket();
    op.post   = pd;
    snprintf(op.key, sizeof(op.key), "%s", g_forum[forum].key);
    if (!pd || !opQueue(op)) {
        // Nothing lost: the message is still in the editor, and /s asks again.
        opFree(pd);
        s.term.nl(s.tl);
        say(s, g_cMark, "--> The board cannot take that just now. /s tries again.");
        bodyPrompt(s);
        return;
    }
    s.term.nl(s.tl);
    walkBegin(s, W_WRITE, T_POSTED, forum, true);
    g_walk[sl].subject = op.ticket;
}

void startPost(Bbs& b, Session& s, bool reply) {
    uint8_t sl = slotIdx(s);
    uint8_t forum = g_at[sl];

    if (forum == 0xFF) {
        notice(s, g_cMark, "--> Open a forum first.");
        noticeDone(b, s);
        return;
    }
    bool allowed = reply ? mayReply(s, forum) : mayStart(s, forum);
    if (!allowed) {
        notice(s, g_cMark, reply ? "--> You cannot post in here."
                                 : "--> You cannot start a subject in here.");
        noticeDone(b, s);
        return;
    }
    if (reply && !g_shown[sl]) {
        notice(s, g_cMark, "--> Read a message first, then R answers it.");
        noticeDone(b, s);
        return;
    }

    g_replying[sl] = reply;
    if (reply) {
        MsgRec m;
        if (readRec(forum, g_shown[sl], m)) {
            g_replyHash[sl] = m.hash;
            snprintf(g_draftSubject[sl], sizeof(g_draftSubject[0]), "%s", m.subject);
        }
        // A reply needs no subject prompt at all: grouping means the subject
        // is drawn once as the screen's title rather than on every message,
        // so there is nothing for the caller to retype.
        bodyBegin(b, s, g_forum[forum].name, g_draftSubject[sl]);
        return;
    }
    g_draftSubject[sl][0] = '\0';
    s.term.cls(s.tl);
    b.rowBar(s, g_cAsk, "New subject");
    s.term.nl(s.tl);
    {
        char q[40];
        snprintf(q, sizeof(q), "Subject (%u max): ", static_cast<unsigned>(kSubject));
        askLine(s, AskSubject, q, kSubject);
    }
}

// ===========================================================================
// Keys.
// ===========================================================================

void listDone(Session& s, bool aborted) {
    // The core hands the session back and draws no prompt of its own,
    // because the plugin owns the screen. Only on an abort: a list that ran
    // to the end has already had its prompt drawn underneath it.
    if (aborted && g_bbs) prompt(*g_bbs, s);
}

void leave(Bbs& b, Session& s) {
    uint8_t sl = slotIdx(s);
    walkStop(sl);
    ptrForget(sl);                      // the read pointer, to the card (1.1.2)
    g_view[sl] = View::Forums;
    g_at[sl]   = 0xFF;
    s.term.nl(s.tl);
    say(s, Color::Grey, "Leaving the forums. Returning to the BBS...");
    b.release(s);
}

void showHelp(Bbs& b, Session& s) {
    s.term.cls(s.tl);
    b.rowTitle(s, "Forums: what the keys do");
    static const char* const kKeys[] = {
        "Enter   the next thing you have not read",
        "12 Ent  a number, then Enter, opens a forum or subject",
        "P       post a new subject here",
        "R       reply to the message on screen",
        "D       remove the message on screen (moderators)",
        "L       back to the list",
        "?       this",
        "Q  ESC  back one level, again to leave",
    };
    for (const char* line : kKeys) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, line);
        s.term.nl(s.tl);
    }
    b.rowRule(s);
    prompt(b, s);
}

// askRemove: D, on the message on screen. Asks before doing anything.
void askRemove(Bbs& b, Session& s) {
    uint8_t sl = slotIdx(s);
    uint8_t forum = g_at[sl];
    if (g_view[sl] != View::Reading || !g_shown[sl] || forum == 0xFF) {
        notice(s, g_cMark, "--> Read a message first, then D removes it.");
        noticeDone(b, s);
        return;
    }
    if (!mayMod(s, forum)) {
        notice(s, g_cMark, "--> You cannot remove messages here.");
        noticeDone(b, s);
        return;
    }
    char q[64];
    snprintf(q, sizeof(q), "--> Remove message #%lu? (y/N)",
             static_cast<unsigned long>(g_shown[sl]));
    notice(s, Color::Yellow, q);
    s.term.ch(s.tl, ' ');
    g_ask[sl] = AskRemove;
}

// removeShown: the answer was yes.
//
// The permission is checked again rather than trusted from the question: a
// CONFIG save between the two can change the forum's levels, and a question
// asked of somebody allowed is not a licence for somebody no longer allowed.
//
// Logged, because a moderation action nobody can audit is indistinguishable
// from a bug (the directory design says the same about holds and bans).
void removeShown(Bbs& b, Session& s) {
    uint8_t sl = slotIdx(s);
    uint8_t forum = g_at[sl];
    uint32_t n = g_shown[sl];
    if (forum == 0xFF || !n || !mayMod(s, forum)) {
        notice(s, g_cMark, "--> You cannot remove messages here.");
        noticeDone(b, s);
        return;
    }
    // To the writer (1.2.1), and the answer in walkFinish (T_REMOVED), which
    // logs it: only a removal that happened is written to the log.
    Op op;
    op.kind  = OP_REMOVE;
    op.forum = forum;
    op.node  = sl;
    op.call  = s.call;
    op.ticket = nextTicket();
    op.num   = n;
    snprintf(op.key, sizeof(op.key), "%s", g_forum[forum].key);
    if (!opQueue(op)) {
        notice(s, Color::LightRed, "--> The board cannot take that just now. D tries again.");
        noticeDone(b, s);
        return;
    }
    s.term.nl(s.tl);
    s.term.nl(s.tl);
    walkBegin(s, W_WRITE, T_REMOVED, forum, true);
    g_walk[sl].subject = op.ticket;
}

// jumpTo: act on a number typed at the prompt.
//
// In the forum list it is the forum's row. Anywhere else it is a subject's
// number, which is the ID of the message that started it, so the number a
// caller types is the number the message shows.
void jumpTo(Bbs& b, Session& s, uint32_t num) {
    uint8_t sl = slotIdx(s);
    g_ask[sl] = AskNone;
    if (g_view[sl] == View::Forums) {
        uint8_t vis[kMaxForums];
        uint8_t n = visibleForums(s, vis, kMaxForums);
        if (num >= 1 && num <= n) { drawSubjects(b, s, vis[num - 1]); return; }
        notice(s, g_cMark, "--> No forum with that number.");
        noticeDone(b, s);
        return;
    }
    // The table may belong to another caller by now. Rescan rather than
    // open whatever sits in somebody else's forum: a walk (1.1.2), with the
    // number carried to its end.
    if (!claims::holds(claims::Res::Subjects, sl) || g_subjFor != g_at[sl]) {
        walkScanBegin(s, g_at[sl], T_JUMP);
        g_walk[sl].arg = num;
        walkRun(b, s);
        return;
    }
    jumpFound(b, s, num);
}

// jumpFound: the subject with this number in the table, opened.
void jumpFound(Bbs& b, Session& s, uint32_t num) {
    uint8_t sl = slotIdx(s);
    for (uint8_t k = 0; k < g_subjRows; ++k) {
        if (g_subjRow[k].first == num) {
            g_subj[sl]  = g_subjRow[k].hash;
            g_shown[sl] = 0;          // from the top of the conversation
            readNext(b, s);
            return;
        }
    }
    notice(s, g_cMark, "--> No subject with that number.");
    noticeDone(b, s);
}

// ---------------------------------------------------------------------------
// walkFinish: a walk has its answer; do what it was for.
// ---------------------------------------------------------------------------
void walkFinish(Bbs& b, Session& s) {
    uint8_t sl = slotOf(s);
    Walk w = g_walk[sl];
    walkStop(sl);
    if (w.shown) s.term.eraseBack(s.tl, 1);            // the spinner off the line
    switch (w.then) {
        case T_SHOW:
            if (w.found) { showMessage(b, s, w.kind == W_ANY ? w.forum : g_at[sl], w.found); return; }
            if (w.kind == W_ANY) {
                notice(s, g_cMark, "--> Nothing new. # - Open a forum to browse it.");
                noticeDone(b, s);
                return;
            }
            if (w.kind == W_SUBJECT) {
                // The end of the conversation. Roll on to whatever else is
                // unread in the forum rather than dead-ending with only Q.
                g_subj[sl] = 0;
                walkBegin(s, W_UNREAD, T_ENDSUBJ, g_at[sl], true);
                g_walk[sl].pos = ptrFor(s, g_at[sl]).mark + 1;
                g_walk[sl].end = g_forum[g_at[sl]].newest;
                walkRun(b, s);
                return;
            }
            notice(s, g_cMark, "--> Nothing new here. L lists the subjects, # - Jump to one.");
            noticeDone(b, s);
            return;
        case T_ENDSUBJ:
            notice(s, g_cMark, w.found ? "--> That is the end of that subject."
                                       : "--> That is the end of that subject. Nothing else new here.");
            if (w.found) { showMessage(b, s, g_at[sl], w.found); return; }
            noticeDone(b, s);
            return;
        case T_DRAWSUBJ:
            drawSubjectsNow(b, s);
            return;
        case T_JUMP:
            jumpFound(b, s, w.arg);
            return;
        case T_DRAWFORUMS:
            drawForums(b, s);
            return;
        case T_POSTED:
            // The forum's figures already say it (opDeliver put them in
            // before this), so the message can be read the moment it is
            // said. The poster has read their own message by definition.
            if (w.found) {
                ptrSeen(s, w.forum, w.found);
                unreadNow(s, w.forum);
                char msg[80];
                snprintf(msg, sizeof(msg), "--> Posted as message %lu.",
                         static_cast<unsigned long>(w.found));
                say(s, Color::LightGreen, msg);
                claims::release(claims::Res::Subjects, sl);
            } else {
                say(s, Color::LightRed, "--> That did not save. The card may be full.");
            }
            g_replying[sl] = false;
            prompt(b, s);
            return;
        case T_REMOVED:
            if (w.found) {
                plat::log("forums: %s removed #%lu from %s", s.user,
                          static_cast<unsigned long>(w.found), g_forum[w.forum].key);
                unreadNow(s, w.forum);
                char msg[48];
                snprintf(msg, sizeof(msg), "--> Message #%lu removed.",
                         static_cast<unsigned long>(w.found));
                say(s, Color::LightGreen, msg);
            } else {
                say(s, w.arg == RES_GONE ? g_cMark : Color::LightRed,
                    w.arg == RES_GONE ? "--> That message was removed already."
                                      : "--> That did not save. Is the card still in?");
            }
            prompt(b, s);
            return;
        default:
            return;                                     // a count, in the background
    }
}

Session* walkSession(uint8_t sl);

// opDeliver: one of the writer's answers (1.2.1). The forum's figures first,
// by its key (a CONFIG save may have renumbered the topics since), so
// whoever reads next finds what was written; then the caller waiting for it,
// if they are still the caller on that node and still waiting.
void opDeliver(Op& op) {
    if (op.head) {
        for (uint8_t i = 0; i < g_forums; ++i) {
            if (!g_forum[i].key[0] || strcmp(g_forum[i].key, op.key) != 0) continue;
            // newest never goes back: answers come in the order they were
            // written, but start() may have read a later header in between.
            if (op.newest > g_forum[i].newest) g_forum[i].newest = op.newest;
            g_forum[i].total = op.total;
        }
    }
    if ((op.kind == OP_POST || op.kind == OP_REMOVE) && op.res == RES_OK)
        g_subjFor = 0xFF;                               // the subject tally is stale for everybody
    if (op.kind == OP_PTR && op.res != RES_OK) plat::log("forums: a read pointer did not save");
    if (op.kind == OP_SEED && op.res != RES_OK) plat::log("forums: could not write the header for %s", op.key);
    opFree(op.post);
    op.post = nullptr;
    // The card's header may be wrong (a removal whose header did not save):
    // recount it from the records, on the runner, like any other write.
    if (op.stale) {
        Op rc;
        rc.kind    = OP_SEED;
        rc.recount = true;
        rc.forum   = op.forum;
        snprintf(rc.key, sizeof(rc.key), "%s", op.key);
        if (!opQueue(rc)) plat::log("forums: %s: the recount was not queued", op.key);
    }

    if (op.node >= BBS_MAX_NODES + 2 || !g_bbs) return;
    Walk& w = g_walk[op.node];
    // Gone, or stopped waiting for this one. W_WRITE keeps its ticket where a
    // subject walk keeps its subject.
    if (w.kind != W_WRITE || w.call != op.call || w.subject != op.ticket) return;
    Session* s = walkSession(op.node);
    if (!s || !g_bbs->owns(*s, g_index)) { walkStop(op.node); return; }
    w.found = op.res == RES_OK ? op.num : 0;
    w.arg   = op.res;
    walkFinish(*g_bbs, *s);
}

// walkRun: a slice now, for the caller who just asked, so a walk that fits
// one finishes before the pass is out and Enter feels the same as ever.
void walkRun(Bbs& b, Session& s) {
    uint16_t budget = kSlice;
    Walk& w = g_walk[slotOf(s)];
    if (w.kind != W_NONE && walkSlice(s, w, budget)) walkFinish(b, s);
}

// unreadNow: this caller's count for a forum, after they read, posted or
// removed. Exact at once when nothing was ever removed from it; otherwise
// that figure now and the count walked in the background, which puts the
// exact one in when it is done.
void unreadNow(Session& s, uint8_t forum) {
    uint8_t sl = slotOf(s);
    setUnread(sl, forum, unreadUpTo(ptrFor(s, forum), g_forum[forum].newest));
    if (g_forum[forum].total >= g_forum[forum].newest) return;
    if (walking(sl)) return;                            // one walk at a time; the next count corrects it
    walkBegin(s, W_COUNT, T_NONE, 0xFF, false);
    g_walk[sl].mask = static_cast<uint16_t>(1u << forum);
}

// walkSession: the caller a walk belongs to, or null when they have gone.
Session* walkSession(uint8_t sl) {
    Session* found = nullptr;
    struct Ctx { uint8_t sl; uint16_t call; Session** out; } c{ sl, g_walk[sl].call, &found };
    Bbs::instance().eachSession([](void* ctx, Session& x) {
        Ctx* k = static_cast<Ctx*>(ctx);
        if (slotOf(x) == k->sl && x.call == k->call && x.st != SState::Free) *k->out = &x;
    }, &c);
    return found;
}

// tick: the walks, a slice each, round robin, at most kTickBudget records
// for everybody in one pass (PF_FAST: every 20 ms).
void tick(uint32_t now) {
    if (!g_bbs) return;
    opsTick();                         // the writer's answers first: a post waited on is done
    uint16_t budget = kTickBudget;
    constexpr uint8_t kN = BBS_MAX_NODES + 2;
    for (uint8_t k = 0; k < kN && budget; ++k) {
        const uint8_t sl = static_cast<uint8_t>((g_walkNext + k) % kN);
        Walk& w = g_walk[sl];
        if (w.kind == W_NONE) continue;
        Session* s = walkSession(sl);
        // Still ours: in the forums, or, for a count in the background, in
        // one of the forums' own lists. A message is a list since 1.2.1, and
        // the count its opening starts (unreadNow) used to be stopped at the
        // first tick because the session was in the core's list state.
        const bool ours = s && (g_bbs->owns(*s, g_index) ||
                                (!w.fg && s->owner == g_index &&
                                 (s->st == SState::List || s->st == SState::More)));
        if (!ours) { walkStop(sl); continue; }
        if (walkSlice(*s, w, budget)) { walkFinish(*g_bbs, *s); continue; }
        // Still going: say so on the line after a moment, as every other
        // wait on the board does.
        if (w.fg && static_cast<int32_t>(now - w.spinAt) >= 0 && s->tl.empty()) {
            if (!w.shown) { s->term.color(s->tl, Color::Yellow); s->term.ch(s->tl, ' '); w.shown = true; }
            s->term.left(s->tl, 1);
            s->term.color(s->tl, Color::Yellow);
            fx::spinFrame(s->term, s->tl, fx::Spin::Line, ++w.spin);
            w.spinAt = now + 150;
        }
    }
    g_walkNext = static_cast<uint8_t>((g_walkNext + 1) % kN);
}

// ---------------------------------------------------------------------------
// liftInput / restoreInput: a page, a broadcast or a ring printed into the
// forums (1.1.0). The line the caller is on is simply ended, and whatever
// they were being asked is asked again underneath, with what they had typed:
// the footer and breadcrumb, a number part typed, a subject, a line of a
// post, or "remove this message?". Nothing they wrote is lost to a notice.
// ---------------------------------------------------------------------------
bool hookLift(Session& s) {
    s.term.reset(s.tl);
    s.term.nl(s.tl);
    return true;
}

void hookRestore(Session& s) {
    if (!g_bbs) return;
    Bbs& b = *g_bbs;
    uint8_t sl = slotIdx(s);
    // A walk or a write the caller is waiting on draws its own answer and
    // prompt when it ends; a prompt drawn here would be a second one, above
    // it. The spinner starts again on the new line.
    if (walking(sl) && g_walk[sl].fg) { g_walk[sl].shown = false; return; }
    switch (g_ask[sl]) {
        case AskSubject: {
            char q[40];
            snprintf(q, sizeof(q), "Subject (%u max): ", static_cast<unsigned>(kSubject));
            s.term.color(s.tl, g_cAsk);
            s.term.text(s.tl, q);
            s.term.color(s.tl, g_cBody);
            s.ed.redraw(s.term, s.tl);
            return;
        }
        case AskBody:
            bodyPrompt(s, true);
            return;
        case AskRemove: {
            char q[64];
            snprintf(q, sizeof(q), "--> Remove message #%lu? (y/N)",
                     static_cast<unsigned long>(g_shown[sl]));
            s.term.nl(s.tl);
            say(s, Color::Yellow, q);
            s.term.ch(s.tl, ' ');
            return;
        }
        case AskJump:
            prompt(b, s);
            s.term.color(s.tl, g_cBody);
            s.ed.redraw(s.term, s.tl);
            return;
        default:
            prompt(b, s);
            return;
    }
}

void onKey(Session& s, int key, uint32_t) {
    if (!g_bbs) return;
    Bbs& b = *g_bbs;
    uint8_t sl = slotIdx(s);

    // A walk the caller is waiting on (1.1.2): ESC or Q stops it; anything
    // else waits, as keys do at every other spinner on the board.
    if (walking(sl) && g_walk[sl].fg) {
        const bool out = key == KEY_ESC || key == KEY_BREAK || key == 'q' || key == 'Q';
        // A write cannot be taken back once queued (1.2.1), so waiting for one
        // is not stopped by a key: the answer is milliseconds away. Only when
        // it has waited long (behind a camera snap on the runner, say) does
        // ESC or Q hand the caller back, and the write still lands.
        if (g_walk[sl].kind == W_WRITE) {
            if (!out || plat::millis() - g_walk[sl].startedAt < kWriteGiveMs) return;
            if (g_walk[sl].shown) s.term.eraseBack(s.tl, 1);
            walkStop(sl);
            g_replying[sl] = false;
            say(s, g_cMark, "--> Still saving. It lands when the card is free.");
            prompt(b, s);
            return;
        }
        if (out) {
            if (g_walk[sl].shown) s.term.eraseBack(s.tl, 1);
            walkStop(sl);
            notice(s, g_cMark, "--> Stopped.");
            noticeDone(b, s);
        }
        return;
    }
    if (walking(sl)) walkStop(sl);          // a count in the background: the key wins

    // A number being typed at the prompt. Digits, Backspace, Enter and ESC
    // mean something; a letter is ignored rather than taken as a command,
    // because the caller is part way through a number.
    // The removal question. Only y removes; anything else keeps it, because
    // a mistyped key must never cost somebody their post.
    if (g_ask[sl] == AskRemove) {
        g_ask[sl] = AskNone;
        if (key == 'y' || key == 'Y') { removeShown(b, s); return; }
        notice(s, g_cMark, "--> Kept.");
        noticeDone(b, s);
        return;
    }

    if (g_ask[sl] == AskJump) {
        if (key == KEY_ENTER) {
            jumpTo(b, s, static_cast<uint32_t>(strtoul(s.ed.text(), nullptr, 10)));
            return;
        }
        if (key == KEY_ESC || (key == KEY_BACKSPACE && s.ed.len() <= 1)) {
            s.term.eraseBack(s.tl, s.ed.shown());
            g_ask[sl] = AskNone;
            return;
        }
        if ((key >= '0' && key <= '9') || key == KEY_BACKSPACE) s.ed.key(key, s.term, s.tl);
        return;
    }

    // A question is open: the keys belong to the editor, not to the menu.
    // Without this a caller typing a subject containing the letter q would
    // be thrown out of the forums mid-sentence.
    if (g_ask[sl] != AskNone) {
        // Word wrap while they type. The editor's buffer is BBS_LINE_MAX and
        // cannot grow, so without this a caller simply stops being echoed
        // mid-sentence, which reads as the board having frozen rather than
        // as a limit. Only for the body: a subject that does not fit is a
        // subject that needs shortening, and silently continuing it onto a
        // second line nobody asked for would be worse.
        // Backspace on an empty line takes the previous line back out of
        // the message and puts it in the editor, so a typo three lines up
        // can be fixed. Without it a committed line is unreachable and the
        // only way back is throwing the whole message away.
        if (g_ask[sl] == AskBody && key == KEY_BACKSPACE && s.ed.len() == 0 &&
            g_bodyRows[sl] > 0) {
            // Find the start of the last line; the newline before it is the
            // boundary, and with no newline the whole body is that line.
            uint16_t start = 0;
            for (uint16_t i = g_bodyLen[sl]; i > 0; --i)
                if (s.compose[i - 1] == '\n') { start = i; break; }

            char back[BBS_LINE_MAX + 1];
            snprintf(back, sizeof(back), "%.*s",
                     static_cast<int>(g_bodyLen[sl] - start), s.compose + start);

            g_bodyLen[sl] = start ? static_cast<uint16_t>(start - 1) : 0;
            s.compose[g_bodyLen[sl]] = '\0';
            --g_bodyRows[sl];

            // Clear the prompt we are standing on, then go UP to the line
            // being recalled and clear that, so the text appears where it
            // was rather than a second time underneath. Without the move up,
            // every recalled line showed twice: Rob's "5:" and "6:" each
            // appeared with two different bodies.
            //
            // left() before eraseEol() is how column 0 is reached: there is
            // no carriage-return primitive above the terminal layer, and
            // Term::ch('\r') is deliberately a no-op. left() is a
            // non-destructive move.
            uint8_t w = Bbs::instance().rowWidth(s);
            s.term.eraseBack(s.tl, kBodyPromptCols);
            s.term.up(s.tl, 1);
            s.term.left(s.tl, w);
            s.term.eraseEol(s.tl, w);
            bodyPrompt(s);
            for (const char* c = back; *c; ++c) s.ed.key(*c, s.term, s.tl);
            return;
        }

        if (g_ask[sl] == AskBody && s.ed.len() >= bodyWidth(s) &&
            key >= ' ' && key < 0x7F) {
            char full[BBS_LINE_MAX + 1];
            snprintf(full, sizeof(full), "%s", s.ed.text());

            // compose::wrapPoint is shared with mail, so both subsystems
            // break a line the same way. It is tested on its own in
            // host/test_compose.cpp, including the word too long to break.
            char carry[BBS_LINE_MAX + 1] = {};
            uint8_t keep = compose::wrapPoint(full, s.ed.len(), carry, sizeof(carry));
            full[keep] = '\0';

            // Rub the carried word off THIS line before it moves down.
            // Those characters were echoed as the caller typed them, so
            // without this the screen shows them twice: Rob's post read
            // "...properly handl" and then "handled" on the next line. The
            // stored text was right and the screen was wrong, which is the
            // worse way round, because the screen is all a caller has.
            //
            // One backspace per character carried, plus the space at the
            // break, which was echoed too and is not carried.
            // Same fix as above: this is what put "cra? ?? ?? ?" on screen
            // where the carried word should have been rubbed out.
            uint8_t rub = static_cast<uint8_t>(s.ed.len() - keep);
            s.term.eraseBack(s.tl, rub);

            if (!bodyAdd(s, full)) {
                s.term.nl(s.tl);
                say(s, g_cMark, "--> That is as much as one message holds. /s saves it.");
                bodyPrompt(s);
                return;
            }
            s.term.nl(s.tl);
            bodyPrompt(s);
            // Put the carried word back, then let the keystroke that caused
            // the wrap land on the end of it, so nothing the caller typed is
            // lost and nothing is typed twice.
            for (const char* c = carry; *c; ++c)
                s.ed.key(*c, s.term, s.tl);
        }
        if (key == KEY_ESC) {
            g_ask[sl] = AskNone;
            g_replying[sl] = false;
            notice(s, g_cMark, "--> Nothing posted.");
            prompt(b, s);
            return;
        }
        LineEditor::Res r = s.ed.key(key, s.term, s.tl);
        if (r == LineEditor::Res::Editing) return;
        if (r == LineEditor::Res::Abort) {
            g_ask[sl] = AskNone;
            g_replying[sl] = false;
            notice(s, g_cMark, "--> Nothing posted.");
            prompt(b, s);
            return;
        }
        answered(s, s.ed.text());
        return;
    }

    if (key == 'q' || key == 'Q' || key == KEY_ESC) {
        // Q goes back ONE level, and Q again leaves. That distinction is
        // what a subsystem has that a command does not.
        if (g_view[sl] == View::Reading || g_view[sl] == View::Subjects) {
            if (g_view[sl] == View::Reading && g_at[sl] != 0xFF) {
                drawSubjects(b, s, g_at[sl]);
                return;
            }
            drawForums(b, s);
            return;
        }
        leave(b, s);
        return;
    }
    if (key == '?')  { showHelp(b, s); return; }
    if (key == 'l' || key == 'L') {
        if (g_at[sl] != 0xFF) drawSubjects(b, s, g_at[sl]);
        else                  drawForums(b, s);
        return;
    }
    if (key == 'p' || key == 'P') { startPost(b, s, false); return; }
    if (key == 'r' || key == 'R') { startPost(b, s, true);  return; }
    if (key == 'd' || key == 'D') { askRemove(b, s);        return; }
    // KEY_ENTER, not '\r'. The terminal layer decodes Enter into a key
    // constant above 0xFF (term.h: KEY_ENTER = 0x100), so comparing against
    // a carriage return silently never matches and the key appears to do
    // nothing at all. The test caught it as "the subject that was read is
    // no longer marked new", which points nowhere near the cause: only the
    // first message was ever shown, because opening a subject is what
    // displayed it and every Enter afterwards was swallowed.
    // Space reads on too, the way a pager's does.
    if (key == KEY_ENTER || key == ' ') { readNext(b, s); return; }

    // A number is typed on the prompt line itself and confirmed with Enter.
    // It used to be one keypress and act at once, which meant only 1 to 9
    // could ever be reached: a forum list allows sixteen and a subject list
    // sixty-four, and subjects are numbered by message ID now, so they run
    // past 9 almost immediately.
    if (key >= '1' && key <= '9') {
        g_ask[sl] = AskJump;
        s.ed.begin(6, LineEditor::F_STAY);
        s.ed.key(key, s.term, s.tl);
        return;
    }
}

// answered: a line the caller typed, taken as whatever was asked for.
void answered(Session& s, const char* text) {
    if (!g_bbs) return;
    Bbs& b = *g_bbs;
    uint8_t sl = slotIdx(s);
    uint8_t what = g_ask[sl];
    g_ask[sl] = AskNone;

    if (what == AskSubject) {
        if (!text || !*text) {                    // no subject, no post
            s.term.nl(s.tl);
            say(s, g_cMark, "--> Nothing posted.");
            g_replying[sl] = false;
            prompt(b, s);
            return;
        }
        snprintf(g_draftSubject[sl], sizeof(g_draftSubject[0]), "%.*s", kSubject, text);
        bodyBegin(b, s, g_forum[g_at[sl]].name, g_draftSubject[sl]);
        return;
    }

    // A body line. /s and /a are commands; everything else is text, blank
    // lines included, because a blank line is how somebody separates
    // paragraphs and ending on one would make that impossible.
    if (compose::isAbort(text)) {
        s.term.nl(s.tl);
        say(s, g_cMark, "--> Nothing posted.");
        g_replying[sl] = false;
        prompt(b, s);
        return;
    }
    if (compose::isSave(text)) {
        if (!g_bodyLen[sl]) {
            s.term.nl(s.tl);
            say(s, g_cMark, "--> Nothing written yet. /a throws it away.");
            bodyPrompt(s);
            return;
        }
        finishPost(b, s, s.compose);
        return;
    }

    if (!bodyAdd(s, text ? text : "")) {
        s.term.nl(s.tl);
        say(s, g_cMark, "--> That is as much as one message holds. /s saves it.");
        bodyPrompt(s);
        return;
    }
    if (g_bodyRows[sl] >= kBodyLines) {
        s.term.color(s.tl, g_cMark);
        s.term.text(s.tl, "--> That is the last line. /s saves it.");
        s.term.nl(s.tl);
    }
    bodyPrompt(s);
}

void enter(Bbs& b, Session& s) {
    if (!plat::sdBase()[0]) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "No card in the board, so no forums.");
        b.prompt(s);
        return;
    }
    if (!b.own(s, g_index)) { b.prompt(s); return; }
    b.setDoing(s, "FORUMS");
    uint8_t sl = slotIdx(s);
    g_shown[sl] = 0;
    g_subj[sl]  = 0;
    g_replying[sl] = false;
    g_ask[sl]   = AskNone;         // see onLogoff

    // Unread counts are per caller, so they are computed on the way in
    // rather than held board-wide. A forum's count in the list and the sum
    // of its subjects' counts are the same number computed the same way,
    // because a forum claiming 12 whose subjects sum to 9 reads as broken.
    //
    // A walk since 1.1.2: exact arithmetic for a forum nothing was ever
    // removed from, a walk of the unseen records only for one that had a
    // removal, a slice a pass, and the list drawn when the counts are in.
    walkStop(sl);
    uint16_t mask = 0;
    for (uint8_t i = 0; i < g_forums; ++i)
        if (g_forum[i].key[0]) mask = static_cast<uint16_t>(mask | (1u << i));

    s.term.reset(s.tl);
    s.term.cls(s.tl);
    if (b.showScreen(s, "forums")) s.term.nl(s.tl);
    walkBegin(s, W_COUNT, T_DRAWFORUMS, 0xFF, true);
    g_walk[sl].mask = mask;
    walkRun(b, s);
}

void onLogoff(Session& s) {
    // Sessions come from a static pool, so anything left here is inherited
    // by whoever dials in next on that node. The landing-flag bug in 0.19.2
    // was exactly this shape one layer up, and it dropped the next caller
    // into a room they never asked for.
    uint8_t sl = slotOf(s);
    g_view[sl]     = View::Forums;
    g_at[sl]       = 0xFF;
    g_subj[sl]     = 0;
    g_shown[sl]    = 0;
    g_replying[sl] = false;
    // g_ask was the one field in this block that was never reset, so a
    // caller who dropped part way through a subject, a post or a number left
    // the next caller on that node with their keys going to an editor they
    // could not see.
    g_ask[sl]      = AskNone;
    g_draftSubject[sl][0] = '\0';
    s.compose[0] = '\0';
    g_bodyLen[sl]  = 0;
    g_bodyRows[sl] = 0;
    // The read pointer in RAM goes to the card, and the walk stops (1.1.2).
    walkStop(sl);
    ptrForget(sl);
}

// FORUMS SCAN: what the board thinks is on the card.
//
// Sysop only, and it exists for phase 1 specifically: the formats are frozen
// here and nothing a caller can see would reveal a header written wrong.
void cmdScan(Bbs& b, Session& s) {
    // Staff only, and checked here. It lists every forum on the card with
    // its message count, including the ones a caller may not read, and
    // nothing checked: COMMANDS.md called it staff only and a comment
    // called it sysop only while anybody who could open the forums could
    // run it. Found writing the 0.22.0 long help.
    if (!plugins::mayUse(s, plugins::levelFor(g_index, 2))) {
        say(s, Color::LightRed, "FORUMS SCAN is for staff.");
        b.prompt(s);
        return;
    }
    b.rowTitle(s, "Forums on the card");
    char line[96];
    for (uint8_t i = 0; i < g_forums; ++i) {
        if (!g_forum[i].key[0]) continue;
        snprintf(line, sizeof(line), "%-12s %6lu msgs  newest %lu",
                 g_forum[i].key,
                 static_cast<unsigned long>(g_forum[i].total),
                 static_cast<unsigned long>(g_forum[i].newest));
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, line);
        s.term.nl(s.tl);
    }
    if (!g_forums) {
        say(s, Color::Grey, "None configured. CONFIG forums sets them up.");
        s.term.nl(s.tl);
    }
    b.rowRule(s);
    b.prompt(s);
}

const Command kCommands[] = {
    { "FORUMS", "", 0, CF_READ, "FORUMS", "message boards by topic",
      [](Bbs& b, Session& s, const char* a, uint32_t) {
          if (a && *a && (!strncasecmp(a, "scan", 4))) { cmdScan(b, s); return; }
          enter(b, s);
      },
      Menu::Main, 4 },
    // The word the message boards were called before 0.21.x, kept so an old
    // habit still lands. Hidden: it is compatibility, not a name, and nothing
    // new should use it (CLAUDE.md, "What things are called").
    { "BULLETIN", "", 0, CF_READ | CF_HIDDEN, "", "",
      [](Bbs& b, Session& s, const char*, uint32_t) { enter(b, s); },
      Menu::Hidden, 99 },
};

// Every topic the board reads, topic1 to topic16, as one PS_GROW group
// (1.1.0). CONFIG offered topic1 to topic4 and nothing else, against a
// plugin that reads sixteen (Rob: "why are we limited to just 4 forum
// topics ... have the list auto expand"). Now the page shows the topics that
// are set and one empty row to add the next, growing a row at a time; once
// that would pass the twelve rows a page has left, the rows become the
// Topics button, which opens a page of all sixteen. The group is the table,
// and the table is checked against kMaxForums, so they cannot drift apart.
const PluginSetting kSettings[] = {
    { "topic",   "Topics",   PS_GROW, 0, 0, 0,  "All sixteen, one row each.", nullptr,
      "All forum topics", "More topics than this page holds: all sixteen, a row each, on their own page." },
    { "topic1",  "Topic 1",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 1" },
    { "topic2",  "Topic 2",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 2" },
    { "topic3",  "Topic 3",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 3" },
    { "topic4",  "Topic 4",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 4" },
    { "topic5",  "Topic 5",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 5" },
    { "topic6",  "Topic 6",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 6" },
    { "topic7",  "Topic 7",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 7" },
    { "topic8",  "Topic 8",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 8" },
    { "topic9",  "Topic 9",  PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 9" },
    { "topic10", "Topic 10", PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 10" },
    { "topic11", "Topic 11", PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 11" },
    { "topic12", "Topic 12", PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 12" },
    { "topic13", "Topic 13", PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 13" },
    { "topic14", "Topic 14", PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 14" },
    { "topic15", "Topic 15", PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 15" },
    { "topic16", "Topic 16", PS_TEXT, 0, 0, 63, nullptr, nullptr, "Forum topic 16" },
};
static_assert(sizeof(kSettings) / sizeof(kSettings[0]) == 1u + kMaxForums,
              "one CONFIG row for every topic the plugin reads, and the Topics button");
static_assert(kMaxForums <= Form::kMaxFields, "all the topics fit the Topics page");

void setting(const char* key, char* out, size_t n) {
    // The Topics button's text: what its page holds.
    if (!strcmp(key, "topic")) { snprintf(out, n, "topics 1 to %u", static_cast<unsigned>(kMaxForums)); return; }
    if (strncmp(key, "topic", 5) != 0) { out[0] = '\0'; return; }
    long k = strtol(key + 5, nullptr, 10);
    if (k < 1 || k > kMaxForums) { out[0] = '\0'; return; }
    const Forum& f = g_forum[k - 1];
    if (!f.key[0]) { out[0] = '\0'; return; }
    snprintf(out, n, "%s | %s | %s", f.key, f.name, f.about);
}

const char* status() {
    static char buf[48];
    uint32_t total = 0;
    for (uint8_t i = 0; i < g_forums; ++i) total += g_forum[i].total;
    snprintf(buf, sizeof(buf), "%u forums, %lu messages",
             static_cast<unsigned>(g_forums), static_cast<unsigned long>(total));
    return buf;
}

// Phase 2 uses these; they are written here because the config they read and
// the format they key on are frozen in phase 1, and splitting that would mean
// designing the permission ladder twice. Referenced so the compiler does not
// warn them away.
void (*const kPhase2Keep[])() = {};
[[maybe_unused]] bool phase2Unused(const Session& s) {
    return mayReply(s, 0) || mayStart(s, 0) || mayMod(s, 0) ||
           subjectHash("x") != 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// The descriptor. Filled POSITIONALLY: a field inserted in the middle
// silently shifts every one after it, so anything new is appended, and that
// is not a style rule here but the only safe edit.
// ---------------------------------------------------------------------------
extern const Plugin kForumsPlugin;
const Plugin kForumsPlugin = {
    "forums",
    "Message boards by topic",
    "0.1",
    0,                        // heapBytes
    64u * 1024u,              // storageBytes: room for an index to grow into
    PF_CORE | PF_SD | PF_FAST, // not PF_ON: a sysop turns the boards on; PF_FAST for the walks (1.1.2)
    PlugLevel::All,           // read
    PlugLevel::Users,         // write: posting wants an account
    PlugLevel::Co1,           // admin
    start,
    stop,
    tick,                     // the walks, a slice a pass (1.1.2)
    nullptr,                  // onConnect
    nullptr,                  // onLogin
    onLogoff,
    onKey,
    status,
    kCommands,
    sizeof(kCommands) / sizeof(kCommands[0]),
    kSettings,
    sizeof(kSettings) / sizeof(kSettings[0]),
    setting,
    rows,
    nullptr,                  // onPresence
    nullptr,                  // onBytes
    nullptr,                  // onRename
    listDone,
    hookLift,                 // liftInput: notices reach the forums (1.1.0)
    hookRestore,              // restoreInput
};
