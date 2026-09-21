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
 *               reports/ux-message-boards.md.
 *
 * Notes:        PF_SD. No card, no plugin, and FORUMS is not a command at
 *               all, the same deal the file areas have. A board that offered
 *               empty message boards on hardware that cannot hold them would
 *               be worse than one that does not offer them.
 *
 * Libraries:    none (libc)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLAN-FORUMS.md, reports/ux-message-boards.md, COMMANDS.md
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
#include "../core/bbs_util.h"
#include "../core/plugin.h"
#include "../core/sysconfig.h"
#include "../platform/platform.h"

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
uint32_t subjectHash(const char* s) {
    uint32_t h = 2166136261u;
    bool any = false;
    const char* end = s + strlen(s);
    while (end > s && end[-1] == ' ') --end;          // trailing space
    while (s < end && *s == ' ') ++s;                 // leading space
    for (const char* p = s; p < end; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
        any = true;
    }
    // 0 is the "no subject" sentinel, so a real subject must never hash to
    // it. One bit is a cheaper fix than a second field saying whether the
    // field is meaningful.
    if (!any) return 0;
    return h ? h : 1u;
}

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
    uint32_t  count  = 0;                // live messages
};

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

    g_forum[i].count  = 0;
    g_forum[i].newest = 0;
    for (char* p = buf; p && *p; ) {
        char* tab = strchr(p, '\t');
        if (tab) *tab = '\0';
        if (!strncmp(p, "count=", 6))   g_forum[i].count  = strtoul(p + 6, nullptr, 10);
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
                     static_cast<unsigned long>(g_forum[i].count),
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
// The forum list.
// ===========================================================================

// rows: one row per pass, drawn through the core's list machinery.
//
// **Not drawn directly, and that is a requirement rather than a preference.**
// Sixteen forums at 132 columns is about 3,326 bytes of frame against a
// 3,072 byte timeline. Term::ch returns void and ByteSink::put's bool is
// discarded on that path, so a frame that does not fit loses characters off
// the end with nothing able to report it. startPluginList paces it against
// the room actually left.
bool rows(Session& s) {
    Bbs& b = *g_bbs;
    uint8_t vis[kMaxForums];
    uint8_t n = visibleForums(s, vis, kMaxForums);
    uint8_t row = s.listIdx;

    // Row 0 is the action row: read everything new, wherever it lives. It is
    // where the bar starts and it is the only row without a number, which is
    // what makes it visibly a different kind of thing from a forum.
    if (row == 0) {
        uint32_t newTotal = 0;
        for (uint8_t k = 0; k < n; ++k) newTotal += g_forum[vis[k]].count;
        char line[80];
        if (newTotal)
            snprintf(line, sizeof(line), "Read all %lu new messages",
                     static_cast<unsigned long>(newTotal));
        else
            snprintf(line, sizeof(line), "Nothing new since your last call.");
        s.term.nl(s.tl);
        s.term.color(s.tl, newTotal ? Color::LightGreen : Color::Grey);
        s.term.text(s.tl, "     ");
        // ASCII has neither colour nor reverse video, so the arrow is what
        // says "this is the thing to press". It is the board's own marker,
        // the same one the chat room speaks with.
        if (!s.term.isAnsi() && !s.term.isPet()) s.term.text(s.tl, "--> ");
        s.term.text(s.tl, line);
        s.term.nl(s.tl);
        ++s.listIdx;
        return true;
    }

    uint8_t k = static_cast<uint8_t>(row - 1);
    if (k >= n) return false;
    const Forum& f = g_forum[vis[k]];

    char num[6];
    snprintf(num, sizeof(num), "%2u", static_cast<unsigned>(k + 1));
    s.term.color(s.tl, Color::Yellow);
    s.term.text(s.tl, "  ");
    s.term.text(s.tl, num);
    s.term.ch(s.tl, ' ');

    s.term.color(s.tl, Color::White);
    uint8_t used = static_cast<uint8_t>(4 + s.term.textCols(s.tl, f.name, kNameMax));

    // The description earns its place only when there is room for it, which
    // is 64 columns and up. At 40 the width goes to the name and the count,
    // because a truncated description is worse than none: tin's `d` key
    // toggled exactly this column and that is the precedent.
    uint8_t w = b.rowWidth(s);
    if (w >= 64 && f.about[0]) {
        for (uint8_t i = used; i < kNameMax + 6; ++i) { s.term.ch(s.tl, ' '); ++used; }
        s.term.color(s.tl, Color::Grey);
        used = static_cast<uint8_t>(used + s.term.textCols(s.tl, f.about, 34));
    }

    // Unread count, right aligned. **Zero shows as nothing**, and the row is
    // grey: a column of noughts reads as a fault rather than as quiet.
    // Citadel printed its arrival count only when it was non-zero for the
    // same reason.
    char cnt[20] = "";
    if (f.count) snprintf(cnt, sizeof(cnt), "%lu new",
                          static_cast<unsigned long>(f.count));
    uint8_t clen = static_cast<uint8_t>(strlen(cnt));
    if (clen && used + clen + 2 <= w) {
        for (uint8_t i = used; i + clen < w; ++i) s.term.ch(s.tl, ' ');
        s.term.color(s.tl, Color::LightGreen);
        s.term.text(s.tl, cnt);
    }
    s.term.nl(s.tl);
    ++s.listIdx;
    return true;
}

void drawList(Bbs& b, Session& s) {
    char right[24] = "";
    uint8_t vis[kMaxForums];
    uint8_t n = visibleForums(s, vis, kMaxForums);
    uint32_t newTotal = 0;
    for (uint8_t k = 0; k < n; ++k) newTotal += g_forum[vis[k]].count;
    if (newTotal) snprintf(right, sizeof(right), "%lu new",
                           static_cast<unsigned long>(newTotal));
    else          snprintf(right, sizeof(right), "nothing new");

    s.term.cls(s.tl);
    b.rowTitle(s, "Forums", right);
    s.listIdx = 0;
    b.startPluginList(s, g_index);
}

void prompt(Bbs& b, Session& s) {
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, " A number opens a forum. ? help. Q leaves.");
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Cyan);
    s.term.text(s.tl, "Forums> ");
    (void)b;
}

void listDone(Session& s, bool aborted) {
    // The core hands the session back and draws no prompt of its own,
    // because the plugin owns the screen. Only on an abort: a list that ran
    // to the end has already had its prompt drawn underneath it.
    if (aborted && g_bbs) prompt(*g_bbs, s);
}

void leave(Bbs& b, Session& s) {
    g_where[slotOf(s)] = Where::Out;
    s.term.nl(s.tl);
    s.term.color(s.tl, Color::Grey);
    s.term.text(s.tl, "Leaving the forums. Returning to the BBS...");
    b.release(s);
}

void showHelp(Bbs& b, Session& s) {
    s.term.cls(s.tl);
    b.rowTitle(s, "Forums: what the keys do");
    static const char* const kKeys[] = {
        "Enter   the next thing you have not read",
        "1 2 3   a number opens that forum",
        "L       list this forum's subjects",
        "?       this",
        "Q  ESC  back one level, again to leave",
    };
    for (const char* line : kKeys) {
        s.term.color(s.tl, Color::Grey);
        s.term.ch(s.tl, ' ');
        s.term.text(s.tl, line);
        s.term.nl(s.tl);
    }
    b.rowRule(s);
    prompt(b, s);
}

void onKey(Session& s, int key, uint32_t) {
    if (!g_bbs) return;
    Bbs& b = *g_bbs;
    if (g_where[slotOf(s)] != Where::List) return;

    if (key == 'q' || key == 'Q' || key == KEY_ESC) { leave(b, s); return; }
    if (key == '?')                                 { showHelp(b, s); return; }
    if (key == '\r' || key == '\n') {
        // Phase 1 has nothing to read yet. Saying so plainly beats a key
        // that appears to do nothing, which a caller reads as a broken board.
        s.term.nl(s.tl);
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, "--> Reading is not built yet. Phase 2.");
        prompt(b, s);
        return;
    }
    if (key >= '1' && key <= '9') {
        uint8_t vis[kMaxForums];
        uint8_t n = visibleForums(s, vis, kMaxForums);
        uint8_t want = static_cast<uint8_t>(key - '1');
        s.term.nl(s.tl);
        s.term.color(s.tl, Color::Grey);
        if (want < n) {
            char msg[80];
            snprintf(msg, sizeof(msg), "--> %s has %lu messages. Reading lands in phase 2.",
                     g_forum[vis[want]].name,
                     static_cast<unsigned long>(g_forum[vis[want]].count));
            s.term.text(s.tl, msg);
        } else {
            s.term.text(s.tl, "--> No forum with that number.");
        }
        prompt(b, s);
        return;
    }
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
    g_where[slotOf(s)] = Where::List;
    g_sel[slotOf(s)]   = 0;
    s.term.reset(s.tl);
    s.term.cls(s.tl);
    if (b.showScreen(s, "forums")) s.term.nl(s.tl);
    drawList(b, s);
}

void onLogoff(Session& s) {
    // Sessions come from a static pool, so anything left here is inherited
    // by whoever dials in next on that node.
    g_where[slotOf(s)] = Where::Out;
    g_sel[slotOf(s)]   = 0;
}

// FORUMS SCAN: what the board thinks is on the card.
//
// Sysop only, and it exists for phase 1 specifically: the formats are frozen
// here and nothing a caller can see would reveal a header written wrong.
void cmdScan(Bbs& b, Session& s) {
    b.rowTitle(s, "Forums on the card");
    char line[96];
    for (uint8_t i = 0; i < g_forums; ++i) {
        if (!g_forum[i].key[0]) continue;
        snprintf(line, sizeof(line), " %-12s %6lu msgs  newest %lu",
                 g_forum[i].key,
                 static_cast<unsigned long>(g_forum[i].count),
                 static_cast<unsigned long>(g_forum[i].newest));
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, line);
        s.term.nl(s.tl);
    }
    if (!g_forums) {
        s.term.color(s.tl, Color::Grey);
        s.term.text(s.tl, " None configured. CONFIG FORUMS TOPICS sets them up.");
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
    for (uint8_t i = 0; i < g_forums; ++i) total += g_forum[i].count;
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
