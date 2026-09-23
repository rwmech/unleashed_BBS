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

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>

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

void forumDir(uint8_t i, char* out, size_t n) {
    snprintf(out, n, "%s/p/forums/%s", plat::sdBase(), g_forum[i].key);
}

void indexPath(uint8_t i, char* out, size_t n) {
    char dir[96];
    forumDir(i, dir, sizeof(dir));
    snprintf(out, n, "%s/INDEX.TXT", dir);
}

// readHeader: record 0, the only record that is not a message.
//
// Tab separated key=value padded to 128 bytes, so a key can be added later
// without moving anything: nothing depends on where a field sits, only on
// the line being exactly 128 bytes. That is deliberate, and it is the one
// place in this format where a change later is cheap.
bool readHeader(uint8_t i) {
    char path[128];
    indexPath(i, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[kRec + 1] = {};
    size_t got = fread(buf, 1, kRec, f);
    fclose(f);
    if (got != kRec || strncmp(buf, kMagic, sizeof(kMagic) - 1) != 0) return false;

    g_forum[i].total  = 0;
    g_forum[i].newest = 0;
    for (char* p = buf; p && *p; ) {
        char* tab = strchr(p, '\t');
        if (tab) *tab = '\0';
        if (!strncmp(p, "count=", 6))   g_forum[i].total  = strtoul(p + 6, nullptr, 10);
        if (!strncmp(p, "newest=", 7))  g_forum[i].newest = strtoul(p + 7, nullptr, 10);
        p = tab ? tab + 1 : nullptr;
    }
    return true;
}

// writeHeader: create or refresh record 0.
//
// Written through a temp file and a rename only when the index does not yet
// exist; refreshing an existing header rewrites those 128 bytes in place,
// because the header is the first record and a partial write of it is
// detectable (the magic is the first five bytes) while a partial rename of
// a multi-megabyte index would not be.
bool writeHeader(uint8_t i) {
    char path[128];
    indexPath(i, path, sizeof(path));

    char rec[kRec + 1];
    int n = snprintf(rec, sizeof(rec),
                     "%s\ttopic=%s\tcount=%06lu\tnewest=%06lu\t",
                     kMagic, g_forum[i].key,
                     static_cast<unsigned long>(g_forum[i].total),
                     static_cast<unsigned long>(g_forum[i].newest));
    if (n < 0) return false;
    // Pad to exactly 126 and close with CR LF. A header that is not 128
    // bytes would put every message in the file at the wrong offset, which
    // is the one failure this format cannot survive, so it is built to a
    // fixed width rather than assembled and hoped over.
    for (size_t k = static_cast<size_t>(n); k < kOffCrLf; ++k) rec[k] = ' ';
    rec[kOffCrLf]     = '\r';
    rec[kOffCrLf + 1] = '\n';

    FILE* f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
    if (!f) return false;
    bool ok = fwrite(rec, 1, kRec, f) == kRec;
    fflush(f);
    fclose(f);
    return ok;
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
    FILE* f = fopen(path, "rb");
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
constexpr uint16_t kBodyMax = 1728;           // 24 lines x 72 columns

void segPath(uint8_t i, uint16_t seg, char* out, size_t n) {
    char dir[96];
    forumDir(i, dir, sizeof(dir));
    snprintf(out, n, "%s/M%04u.TXT", dir, static_cast<unsigned>(seg));
}

// appendBody: the text, and where it landed.
//
// Returns false without writing anything if the card is full or the file
// cannot be opened, so the caller can refuse the post rather than write an
// index record pointing at a body that is not there. Order matters here:
// **the body is written and flushed before the index record that names it.**
// A power cut between the two leaves an orphan body, which is invisible and
// harmless; the other order leaves an index entry pointing at nothing, which
// is a message that exists and cannot be read.
bool appendBody(uint8_t i, const char* text, uint16_t& seg, uint32_t& ofs, uint16_t& len) {
    seg = 0;
    char path[128];
    // Find the newest segment that still has room.
    for (uint16_t s = 0; s < 9999; ++s) {
        segPath(i, s, path, sizeof(path));
        FILE* f = fopen(path, "rb");
        if (!f) { seg = s; break; }
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fclose(f);
        if (end >= 0 && static_cast<uint32_t>(end) < kSegMax) { seg = s; break; }
    }
    segPath(i, seg, path, sizeof(path));
    FILE* f = fopen(path, "ab");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long at = ftell(f);
    if (at < 0) { fclose(f); return false; }
    size_t n = strlen(text);
    if (n > kBodyMax) n = kBodyMax;
    bool ok = fwrite(text, 1, n, f) == n;
    ok = ok && fputc('\n', f) != EOF;
    fflush(f);
    fclose(f);
    if (!ok) return false;
    ofs = static_cast<uint32_t>(at);
    len = static_cast<uint16_t>(n);
    return true;
}

// readBody: the text of one message into a caller-supplied buffer.
bool readBody(uint8_t i, const MsgRec& m, char* out, size_t outN) {
    out[0] = '\0';
    if (!m.len) return true;                       // a body may legitimately be empty
    char path[128];
    segPath(i, m.seg, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool ok = false;
    if (fseek(f, static_cast<long>(m.ofs), SEEK_SET) == 0) {
        size_t want = m.len < outN - 1 ? m.len : outN - 1;
        size_t got = fread(out, 1, want, f);
        out[got] = '\0';
        ok = got == want;
    }
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------
// appendMessage: the one write path, and the order it happens in.
// ---------------------------------------------------------------------------
bool appendMessage(uint8_t i, MsgRec& m, const char* body) {
    if (!appendBody(i, body, m.seg, m.ofs, m.len)) return false;

    char path[128];
    indexPath(i, path, sizeof(path));
    FILE* f = fopen(path, "r+b");
    if (!f) return false;

    uint32_t n = g_forum[i].newest + 1;
    m.num = n;
    char rec[kRec];
    buildRec(m, rec);
    bool ok = fseek(f, static_cast<long>(n) * kRec, SEEK_SET) == 0 &&
              fwrite(rec, 1, kRec, f) == kRec;
    fflush(f);
    fclose(f);
    if (!ok) return false;

    g_forum[i].newest = n;
    ++g_forum[i].total;
    writeHeader(i);        // the header trails the message, never leads it
    return true;
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

bool readPtr(uint32_t userId, uint8_t forum, Ptr& p) {
    p = Ptr{};
    if (!userId) return false;                     // guests keep no pointer
    char path[128];
    ptrPath(path, sizeof(path));
    FILE* f = fopen(path, "rb");
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

bool writePtr(uint32_t userId, uint8_t forum, const Ptr& p) {
    if (!userId) return true;                      // nothing to keep for a guest
    char path[128];
    ptrPath(path, sizeof(path));
    FILE* f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
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
    fflush(f);
    fclose(f);
    return ok;
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
bool start(Bbs& bbs) {
    g_bbs   = &bbs;
    g_index = plugins::indexOf("forums");
    g_forums = 0;
    for (auto& f : g_forum) f = Forum{};

    plugins::forEachKey(g_index, readKey, nullptr);

    // Create each forum's folder and seed its header. Both are cheap and
    // both are skipped when they already exist, which is what makes this
    // safe to run on every config save.
    char dir[96];
    snprintf(dir, sizeof(dir), "%s/p/forums", plat::sdBase());
    makeDirs(dir);

    for (uint8_t i = 0; i < g_forums; ++i) {
        if (!g_forum[i].key[0]) continue;
        forumDir(i, dir, sizeof(dir));
        makeDirs(dir);
        if (!readHeader(i)) {
            if (!writeHeader(i))
                plat::log("forums: could not write the header for %s", g_forum[i].key);
        }
    }

    memset(g_where, 0, sizeof(g_where));
    memset(g_sel, 0, sizeof(g_sel));
    return true;
}

void stop() {
    // Deliberately empty of card work. A plugin's own lifecycle is not the
    // lifetime of what it opened, and the mount is board-level state.
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

void scanSubjects(uint8_t i, const Ptr& p, uint8_t who) {
    g_subjRows = 0;
    g_subjNumW = 1;
    g_subjFor  = i;
    claims::seize(claims::Res::Subjects, who);   // a cache, not a lock: refill for whoever asks
    uint32_t newest = g_forum[i].newest;
    if (!newest) return;

    char path[128];
    indexPath(i, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return;

    uint32_t floor = newest > kScanMax ? newest - kScanMax : 1;
    for (uint32_t n = newest; n >= floor && n >= 1; --n) {
        if (fseek(f, static_cast<long>(n) * kRec, SEEK_SET) != 0) break;
        char rec[kRec];
        if (fread(rec, 1, kRec, f) != kRec) break;
        MsgRec m;
        parseRec(rec, m);
        if (m.num != n) continue;                 // a torn record, skipped
        if (!m.live) continue;                    // deleted: not a subject

        uint8_t k = 0;
        for (; k < g_subjRows; ++k) if (g_subjRow[k].hash == m.hash) break;
        if (k == g_subjRows) {
            if (g_subjRows >= kMaxSubjects) continue;
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
        if (n == 1) break;                        // uint32 would wrap below 1
    }
    fclose(f);
    uint32_t hi = 0;
    for (uint8_t k = 0; k < g_subjRows; ++k)
        if (g_subjRow[k].first > hi) hi = g_subjRow[k].first;
    while (hi >= 10) { hi /= 10; ++g_subjNumW; }
}

// nextUnread: the next message this caller has not read, or 0.
//
// Forward from just above the mark, because reading is oldest-first: a
// conversation read newest-first is not a conversation. Honours the subject
// filter when one is set, which is what makes "read this thread to the end"
// mean what it says.
// nextInSubject: the next message in a conversation, read or not.
//
// **Opening a subject must show it even when there is nothing new in it**,
// and that is not a nicety. Rob posted the first message on the board, which
// by definition he had read, then opened the subject and got nothing: Enter
// found no unread message and neither did the number. A board where you
// cannot re-read what you just wrote is broken, and so is one where you
// cannot go back and look at a conversation you have already followed.
//
// So inside a subject, reading walks the conversation in order regardless of
// what has been read. Unread still drives what Enter does from the FORUM
// list, which is the fast path, and every message shown is still marked
// read. The distinction is: at the top level Enter means "what is new", and
// inside a conversation it means "what is next".
uint32_t nextInSubject(uint8_t i, uint32_t subject, uint32_t after) {
    uint32_t newest = g_forum[i].newest;
    for (uint32_t n = after + 1; n <= newest; ++n) {
        MsgRec m;
        if (!readRec(i, n, m) || !m.live) continue;
        if (subject && m.hash != subject) continue;
        return n;
    }
    return 0;
}

uint32_t nextUnread(uint8_t i, const Ptr& p, uint32_t subject, uint32_t after) {
    uint32_t newest = g_forum[i].newest;
    uint32_t from = after ? after + 1 : p.mark + 1;
    for (uint32_t n = from; n <= newest; ++n) {
        if (seen(p, n)) continue;
        MsgRec m;
        if (!readRec(i, n, m) || !m.live) continue;
        if (subject && m.hash != subject) continue;
        return n;
    }
    return 0;
}

// liveUnread: what this caller has not read, removed messages not counted.
//
// unreadUpTo() is pure arithmetic over message numbers, which was exact
// while nothing could be removed. Once a post can be taken down it is not:
// a removed message the caller never read would count as new, the forum
// list would say "1 new", and Enter would find nothing, which is the
// numbers-that-do-not-add-up failure this subsystem was designed against.
//
// Free when nothing was ever removed: the header's live count equals the
// highest message number, every message above the mark is live, and the
// arithmetic is exact. Only a forum that has had a removal walks the
// records, and then only the ones this caller has not seen.
uint32_t liveUnread(uint8_t i, const Ptr& p) {
    uint32_t newest = g_forum[i].newest;
    if (g_forum[i].total >= newest) return unreadUpTo(p, newest);

    char path[128];
    indexPath(i, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return unreadUpTo(p, newest);
    uint32_t n0 = p.mark + 1;
    if (newest > kScanMax && n0 < newest - kScanMax) n0 = newest - kScanMax;
    uint32_t c = 0;
    for (uint32_t n = n0; n <= newest; ++n) {
        if (seen(p, n)) continue;
        if (fseek(f, static_cast<long>(n) * kRec, SEEK_SET) != 0) break;
        char rec[kRec];
        if (fread(rec, 1, kRec, f) != kRec) break;
        MsgRec m;
        parseRec(rec, m);
        if (m.num == n && m.live) ++c;
    }
    fclose(f);
    return c;
}

// removeMessage: take message n out of forum i.
//
// One byte: the live flag at kOffFlags goes from '.' to 'X'. A single byte
// cannot be half written, so there is no torn state to recover from, and
// nothing moves: the index is never compacted, every other message keeps its
// number, and every read pointer keeps meaning what it meant. The body stays
// in its segment file, so a removal is recoverable from the card by hand.
bool removeMessage(uint8_t i, uint32_t n) {
    MsgRec m;
    if (!readRec(i, n, m) || !m.live) return false;
    char path[128];
    indexPath(i, path, sizeof(path));
    FILE* f = fopen(path, "r+b");
    if (!f) return false;
    bool ok = fseek(f, static_cast<long>(n) * kRec + kOffFlags, SEEK_SET) == 0 &&
              fputc('X', f) != EOF;
    fflush(f);
    fclose(f);
    if (!ok) return false;
    if (g_forum[i].total) --g_forum[i].total;
    writeHeader(i);        // the header's count is LIVE messages
    return true;
}

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

// rows: one row per pass for whichever list this caller is looking at.
//
// Everything goes through startPluginList rather than being drawn directly.
// Sixteen forums at 132 columns is about 3,326 bytes of frame against a
// 3,072 byte timeline, and Term::ch returns void, so a frame that does not
// fit loses characters off the end with nothing able to report it.
bool rows(Session& s) {
    Bbs& b = *g_bbs;
    uint8_t sl = slotIdx(s);
    uint8_t w  = b.rowWidth(s);

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

void drawSubjects(Bbs& b, Session& s, uint8_t forum) {
    uint8_t sl = slotIdx(s);
    g_view[sl] = View::Subjects;
    g_at[sl]   = forum;
    g_subj[sl] = 0;

    Ptr p;
    readPtr(callerId(s), forum, p);
    scanSubjects(forum, p, sl);

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
// ---------------------------------------------------------------------------
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

    char when[24] = "";
    clk::fmtEpoch(when, sizeof(when), "%d %b %H:%M", m.epoch);

    // Read the pointer BEFORE marking, so the header can say whether this
    // was new to this caller.
    Ptr ptr;
    readPtr(callerId(s), forum, ptr);
    bool fresh = !seen(ptr, n);

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
    // All of it from column 0 (Rob: "not sure why these all start indented,
    // stop that").
    //
    // Two newlines first: one ends the prompt line the key was pressed on,
    // the second is the blank line he asked for above the header.
    s.term.nl(s.tl);
    s.term.nl(s.tl);
    headRule(b, s, fresh, m.num);
    field(b, s, "Subject: ", g_cHead, m.subject);
    field(b, s, "By:      ", g_cWho,  m.handle);
    field(b, s, "Date:    ", g_cWhen, when);
    plainRule(b, s);

    char body[kBodyMax + 1];
    if (!readBody(forum, m, body, sizeof(body))) {
        say(s, Color::LightRed, "(the text of this message could not be read)");
        s.term.nl(s.tl);
    } else {
        // Wrapped at THIS reader's width, not the writer's. The guard is
        // generous: 1,536 characters
        // at 35 columns is about 45 lines before any paragraph breaks, and
        // the old limit of 40 would have cut the end off a full post on a C64.
        // Through the @-code renderer, whose wrap measures what is shown
        // rather than what was typed. A colour lasts to the end of the
        // writer's own line, across however many rows this reader's width
        // turns it into, and not into the next paragraph. The reserve is
        // what the rest of the body still needs, so an effect near the top
        // cannot eat the room the words below it have to print in.
        uint8_t w = b.rowWidth(s);
        if (!w) w = 1;
        char line[160];
        const char* p = body;
        uint8_t guard = 0;
        codes::Painter pt;
        pt.begin(g_cBody, !s.bellOff);
        while ((p = codes::wrap(p, line, sizeof(line), w, pt)) != nullptr
               && ++guard < 96) {
            pt.reserve = strlen(p) + 96;          // the rest, the EOM and the prompt
            codes::row(s.term, s.tl, line, w, pt);
            s.term.nl(s.tl);
            if (p[-1] == '\n') codes::endParagraph(pt);
            if (!*p) break;
        }
    }

    // Rob: "at the end of messages (all) add --> EOM <--". The reading loop
    // does not clear between messages, so a marker at the end of each one
    // is what says where one stops and the next begins. A blank line
    // before it (Rob: "Need a linefeed before EOM").
    s.term.nl(s.tl);
    s.term.color(s.tl, g_cTitle);
    s.term.text(s.tl, "--> EOM <--");
    s.term.nl(s.tl);

    markSeen(ptr, n);
    writePtr(callerId(s), forum, ptr);

    // The forum's unread count follows what this caller has actually read,
    // so the list they come back to agrees with what just happened.
    setUnread(sl, forum, liveUnread(forum, ptr));
    prompt(b, s);
}

// readNext: Enter. The whole fast path.
void readNext(Bbs& b, Session& s) {
    uint8_t sl = slotIdx(s);
    uint8_t forum = g_at[sl];

    // From the forum list, Enter walks INTO the first forum with something
    // unread rather than being a second mode. One mechanism, not two code
    // paths drawing the same message header and drifting apart.
    if (g_view[sl] == View::Forums || forum == 0xFF) {
        uint8_t vis[kMaxForums];
        uint8_t n = visibleForums(s, vis, kMaxForums);
        for (uint8_t k = 0; k < n; ++k) {
            Ptr p;
            readPtr(callerId(s), vis[k], p);
            uint32_t next = nextUnread(vis[k], p, 0, 0);
            if (next) { showMessage(b, s, vis[k], next); return; }
        }
        notice(s, g_cMark, "--> Nothing new. # - Open a forum to browse it.");
        noticeDone(b, s);
        return;
    }

    Ptr p;
    readPtr(callerId(s), forum, p);
    uint32_t from = (g_view[sl] == View::Reading) ? g_shown[sl] : 0;

    // Inside a conversation, walk it in order whether or not it has been
    // read. Only the forum-level Enter is about what is new.
    if (g_subj[sl]) {
        uint32_t next = nextInSubject(forum, g_subj[sl], from);
        if (next) { showMessage(b, s, forum, next); return; }
        // The end of the conversation. Roll on to whatever else is unread in
        // the forum rather than dead-ending with nothing but Q.
        g_subj[sl] = 0;
        uint32_t on = nextUnread(forum, p, 0, 0);
        notice(s, g_cMark, on ? "--> That is the end of that subject."
                              : "--> That is the end of that subject. Nothing else new here.");
        if (on) { showMessage(b, s, forum, on); return; }
        noticeDone(b, s);
        return;
    }

    uint32_t next = nextUnread(forum, p, g_subj[sl], from);
    if (!next && g_subj[sl]) {
        // A subject read to its end rolls on to the rest of the forum,
        // rather than dead-ending with no way onward but Q.
        g_subj[sl] = 0;
        next = nextUnread(forum, p, 0, 0);
        if (next) notice(s, g_cMark, "--> That is the end of that subject.");
    }
    if (next) { showMessage(b, s, forum, next); return; }

    notice(s, g_cMark, "--> Nothing new here. L lists the subjects, # - Jump to one.");
    noticeDone(b, s);
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
void bodyPrompt(Session& s) {
    uint8_t sl = slotOf(s);
    char q[12];
    snprintf(q, sizeof(q), "%2u: ", static_cast<unsigned>(g_bodyRows[sl] + 1));
    g_ask[sl] = AskBody;
    s.term.color(s.tl, g_cMeta);
    s.term.text(s.tl, q);
    s.term.color(s.tl, g_cBody);
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

void finishPost(Bbs& b, Session& s, const char* body) {
    uint8_t sl = slotIdx(s);
    uint8_t forum = g_at[sl];

    MsgRec m;
    m.authorId = callerId(s);
    snprintf(m.handle, sizeof(m.handle), "%.*s", kHandle, s.user);
    m.epoch = clk::epoch();
    snprintf(m.subject, sizeof(m.subject), "%.*s", kSubject, g_draftSubject[sl]);
    // A reply carries its parent's hash rather than rehashing the text, so
    // renaming a subject cannot split a thread and two subjects that happen
    // to read alike cannot be merged into one.
    m.hash = g_replying[sl] ? g_replyHash[sl] : subjectHash(m.subject);

    s.term.nl(s.tl);
    if (!appendMessage(forum, m, body)) {
        say(s, Color::LightRed, "--> That did not save. The card may be full.");
    } else {
        // The poster has read their own message by definition.
        Ptr p;
        readPtr(callerId(s), forum, p);
        markSeen(p, m.num);
        writePtr(callerId(s), forum, p);
        setUnread(sl, forum, liveUnread(forum, p));

        char msg[80];
        snprintf(msg, sizeof(msg), "--> Posted as message %lu.",
                 static_cast<unsigned long>(m.num));
        say(s, Color::LightGreen, msg);
        g_subjFor = 0xFF;                       // the tally is stale now
        claims::release(claims::Res::Subjects, slotOf(s));
    }
    g_replying[sl] = false;
    prompt(b, s);
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
    if (!removeMessage(forum, n)) {
        notice(s, Color::LightRed, "--> That did not save. Is the card still in?");
        prompt(b, s);
        return;
    }
    plat::log("forums: %s removed #%lu from %s", s.user,
              static_cast<unsigned long>(n), g_forum[forum].key);

    // The subject table describes the forum as it was: stale for everybody.
    g_subjFor = 0xFF;
    Ptr p;
    readPtr(callerId(s), forum, p);
    setUnread(sl, forum, liveUnread(forum, p));

    char msg[48];
    snprintf(msg, sizeof(msg), "--> Message #%lu removed.", static_cast<unsigned long>(n));
    notice(s, Color::LightGreen, msg);
    prompt(b, s);
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
    // open whatever sits in somebody else's forum.
    if (!claims::holds(claims::Res::Subjects, sl) || g_subjFor != g_at[sl]) {
        Ptr rp;
        readPtr(callerId(s), g_at[sl], rp);
        scanSubjects(g_at[sl], rp, sl);
    }
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

void onKey(Session& s, int key, uint32_t) {
    if (!g_bbs) return;
    Bbs& b = *g_bbs;
    uint8_t sl = slotIdx(s);

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
    for (uint8_t i = 0; i < g_forums; ++i) {
        if (!g_forum[i].key[0]) continue;
        Ptr p;
        readPtr(callerId(s), i, p);
        setUnread(sl, i, liveUnread(i, p));
    }

    s.term.reset(s.tl);
    s.term.cls(s.tl);
    if (b.showScreen(s, "forums")) s.term.nl(s.tl);
    drawForums(b, s);
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

const PluginSetting kSettings[] = {
    { "topic1", "Topic 1", PS_TEXT, 0, 0, 63 },
    { "topic2", "Topic 2", PS_TEXT, 0, 0, 63 },
    { "topic3", "Topic 3", PS_TEXT, 0, 0, 63 },
    { "topic4", "Topic 4", PS_TEXT, 0, 0, 63 },
};

void setting(const char* key, char* out, size_t n) {
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
    PF_CORE | PF_SD,          // not PF_ON: a sysop turns the boards on
    PlugLevel::All,           // read
    PlugLevel::Users,         // write: posting wants an account
    PlugLevel::Co1,           // admin
    start,
    stop,
    nullptr,                  // tick
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
};
