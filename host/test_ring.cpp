// Checks for the sysop page's rules, with no board, session or file in
// them (1.1.0): how often a caller may ring, and how a ring nobody answered
// is written down and read back.
//
// Every rate rule is a boundary: three minutes to the millisecond, the third
// ring and the fourth, "2 minutes" against "3". A rule off by one is either
// a caller told to wait when they need not, or a sysop rung more than the
// board promised, and neither shows in a reading of the code. The notes are
// a file format, so they are checked the way formats are checked here: what
// goes in comes back, and what is damaged is refused rather than half read.
//
// The ring itself, the question, the room and the notes file on disk are
// tested end to end by tools/testclient.py (test_operator and friends).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/core/ring.h"
#include "../src/core/bus.h"

using namespace ring;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

int main() {
    printf("Sysop page: the rate rules\n");
    {
        Limiter<4> l;
        uint8_t m = 99;
        check("a fresh caller may ring", l.check(1, 5000, false, m) == Verdict::Ok && m == 0);
        check("unless somebody else is ringing", l.check(1, 5000, true, m) == Verdict::Board);
        l.spend(1, 5000);
        check("straight after a ring: wait", l.check(1, 5000, false, m) == Verdict::Wait);
        check("and it says 3 minutes, rounded up", m == 3);
        check("one millisecond short of the gap is still a wait",
              l.check(1, 5000 + kGapMs - 1, false, m) == Verdict::Wait && m == 1);
        check("the gap exactly is allowed", l.check(1, 5000 + kGapMs, false, m) == Verdict::Ok);
        check("120,001 ms left is 3 minutes",
              l.check(1, 5000 + kGapMs - 120001, false, m) == Verdict::Wait && m == 3);
        check("120,000 ms left is 2 minutes",
              l.check(1, 5000 + kGapMs - 120000, false, m) == Verdict::Wait && m == 2);
        check("60,001 ms left is 2 minutes",
              l.check(1, 5000 + kGapMs - 60001, false, m) == Verdict::Wait && m == 2);
        check("60,000 ms left is 1 minute",
              l.check(1, 5000 + kGapMs - 60000, false, m) == Verdict::Wait && m == 1);
        check("another slot is not affected", l.check(2, 5000, false, m) == Verdict::Ok);
        check("a wait is said before the board being busy",
              l.check(1, 5001, true, m) == Verdict::Wait);
    }
    {
        Limiter<4> l;
        uint8_t m = 0;
        uint32_t t = 1000;
        for (uint8_t i = 0; i < kPerCall; ++i) {
            check("each of the rings a call allows is allowed",
                  l.check(3, t, false, m) == Verdict::Ok);
            l.spend(3, t);
            t += kGapMs;
        }
        check("and the next one is not, however long the wait",
              l.check(3, t + 10 * kGapMs, false, m) == Verdict::Call);
        check("the call's limit is said before the board being busy",
              l.check(3, t + 10 * kGapMs, true, m) == Verdict::Call);
        check("used() counts them", l.used(3) == kPerCall);
        l.forget(3);
        check("a new call on the slot starts again", l.check(3, t, false, m) == Verdict::Ok &&
                                                     l.used(3) == 0);
    }
    {
        Limiter<4> l;
        uint8_t m = 0;
        l.spend(1, 0xFFFFF000u);
        check("the clock wrapping does not free a caller early",
              l.check(1, 0x00001000u, false, m) == Verdict::Wait);
        l.move(1, 0);
        check("move: the allowance goes with the caller",
              l.check(0, 0x00001000u, false, m) == Verdict::Wait && l.used(0) == 1);
        check("and the slot they left is clear",
              l.check(1, 0x00001000u, false, m) == Verdict::Ok && l.used(1) == 0);
        check("a slot past the end is refused, not written",
              l.check(9, 0, false, m) == Verdict::Call);
        l.spend(9, 0);
        l.forget(9);
        l.move(9, 1);
        check("and spending, forgetting and moving it do nothing", l.used(1) == 0);
    }

    printf("Sysop page: the notes\n");
    {
        Note n;
        n.epoch = 1790000000u;
        n.node  = 3;
        n.guest = false;
        snprintf(n.handle, sizeof(n.handle), "quantumrob");
        snprintf(n.reason, sizeof(n.reason), "can't upload to Drop Box");
        char line[160];
        formatNote(n, line, sizeof(line));
        check("a note is one line", strchr(line, '\n') == line + strlen(line) - 1);
        Note back;
        check("and reads back", parseNote(line, back));
        check("every field the same",
              back.epoch == n.epoch && back.node == 3 && !back.guest &&
              !strcmp(back.handle, "quantumrob") && !strcmp(back.reason, n.reason));
    }
    {
        Note n;
        n.node  = 10;
        n.guest = true;
        snprintf(n.handle, sizeof(n.handle), "Visitor");
        snprintf(n.reason, sizeof(n.reason), "a\ttab and\na newline");
        char line[160];
        formatNote(n, line, sizeof(line));
        Note back;
        check("a guest and no clock read back", parseNote(line, back) && back.guest &&
                                                  back.epoch == 0 && back.node == 10);
        check("a tab or newline in the reason becomes a space",
              !strcmp(back.reason, "a tab and a newline"));
    }
    {
        Note n;
        snprintf(n.handle, sizeof(n.handle), "%s", "abcdefghijklmnopqrst");     // 20
        memset(n.reason, 'r', kReasonMax);
        n.reason[kReasonMax] = '\0';
        char line[160];
        formatNote(n, line, sizeof(line));
        Note back;
        check("the longest handle and reason fit",
              parseNote(line, back) && strlen(back.handle) == 20 &&
              strlen(back.reason) == kReasonMax);
    }
    {
        Note back;
        check("a line cut short by a power cut is refused",
              !parseNote("1790000000\t3\t0\tquantumrob\tcan't upl", back));
        check("a line with no handle is refused", !parseNote("1\t3\t0\t\treason\n", back));
        check("a handle too long is refused",
              !parseNote("1\t3\t0\tabcdefghijklmnopqrstu\treason\n", back));
        check("a guest flag that is not 0 or 1 is refused",
              !parseNote("1\t3\t2\tbob\treason\n", back));
        check("a node past 255 is refused", !parseNote("1\t300\t0\tbob\treason\n", back));
        check("text that is not a note is refused", !parseNote("hello\n", back));
        check("the header is not a note", !parseNote("rings 3\n", back));
        check("an empty reason is still a note", parseNote("1\t3\t0\tbob\t\n", back) &&
                                                 !back.reason[0]);
    }
    {
        char line[32];
        uint16_t total = 0;
        formatHeader(12, line, sizeof(line));
        check("the header reads back", parseHeader(line, total) && total == 12);
        check("a note is not the header", !parseHeader("1\t3\t0\tbob\tr\n", total));
        check("a header with no number is refused", !parseHeader("rings \n", total));
        check("a huge count is held at 65535", parseHeader("rings 999999\n", total) &&
                                               total == 65535);
    }

    printf("Sysop page: the ring in a full queue\n");
    {
        Mailbox mb;
        BusMsg m;
        m.kind = BusKind::Ring;
        snprintf(m.text, sizeof(m.text), "7");
        mb.push(m);
        for (int i = 0; i < BBS_BUS_DEPTH + 3; ++i) {
            BusMsg p;
            p.kind = BusKind::Arrival;
            snprintf(p.text, sizeof(p.text), "arrival %d", i);
            mb.push(p);
        }
        check("a burst of arrivals does not push the ring out",
              mb.has(Mailbox::bit(BusKind::Ring)));
        BusMsg out;
        check("take finds it among them", mb.take(Mailbox::bit(BusKind::Ring), out) &&
                                           out.kind == BusKind::Ring && !strcmp(out.text, "7"));
        check("and it is gone after", !mb.has(Mailbox::bit(BusKind::Ring)));
        // What is left is the newest arrivals, oldest first, in order.
        bool order = true;
        int n = 0, last = -1;
        while (mb.pop(out)) {
            int v = atoi(out.text + 8);
            if (out.kind != BusKind::Arrival || v <= last) order = false;
            last = v;
            ++n;
        }
        check("the rest keep their order", order && n == BBS_BUS_DEPTH - 1);
        check("and the newest survived", last == BBS_BUS_DEPTH + 2);
    }
    {
        Mailbox mb;
        for (int i = 0; i < BBS_BUS_DEPTH; ++i) {
            BusMsg p;
            p.kind = i == 2 ? BusKind::Broadcast : BusKind::Page;
            snprintf(p.text, sizeof(p.text), "%d", i);
            mb.push(p);
        }
        BusMsg out;
        check("take pulls a broadcast from the middle",
              mb.take(Mailbox::bit(BusKind::Broadcast), out) && !strcmp(out.text, "2"));
        const char* want[] = { "0", "1", "3" };
        bool ok = true;
        for (const char* w : want) ok = ok && mb.pop(out) && !strcmp(out.text, w);
        check("and the pages around it keep their order", ok && !mb.pop(out));
    }

    printf("Sysop page: how long a ring rings\n");
    {
        setenv("BBS_RING_MS", "8000", 1);
        check("the host takes BBS_RING_MS, so the suite need not wait 45 s",
              ringMs() == 8000);
        check("45 s on a board", kRingMs == 45000);
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
