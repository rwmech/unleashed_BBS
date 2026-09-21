// Checks for the forums read pointer, with no board, card or session in it.
//
// This is the piece of the forums most likely to be subtly wrong and least
// likely to LOOK wrong, which is the combination that earns a unit test. The
// failure it guards against is not a crash: it is a board that quietly tells
// somebody they have three unread messages in a subject they just read to
// the end, which is the first thing anybody checks and the fastest way to
// stop trusting a board.
//
// It also pins the failure DIRECTION, which is a design decision rather than
// an implementation detail: forgetting that something was read is acceptable
// and hiding something unread is not.
#include <cstdio>
#include "../src/plugins/forums_ptr.h"

using namespace forumptr;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

int main() {
    printf("forums read pointer\n");

    // ---- the ordinary case: reading in order costs nothing --------------
    {
        Ptr p;
        for (uint32_t n = 1; n <= 500; ++n) markSeen(p, n);
        bool clean = p.mark == 500;
        for (uint8_t k = 0; k < kWindowBytes; ++k) if (p.win[k]) clean = false;
        check("reading 500 in order leaves the mark at 500 and the window empty",
              clean);
    }

    // ---- Rob's own example, walked through ------------------------------
    // A forum at message 109. The caller has read up to 99. Two subjects
    // interleave: "tips" is 100, 104, 109 and "antennas" is everything else.
    // Reading the tips subject to its end must not declare antennas read.
    {
        Ptr p;
        p.mark = 99;
        markSeen(p, 100);
        markSeen(p, 104);
        markSeen(p, 109);

        check("reading a subject out of order does not move the mark past the gap",
              p.mark == 100);           // 100 drained, 104 and 109 still held
        check("the messages actually read are remembered",
              seen(p, 100) && seen(p, 104) && seen(p, 109));
        check("and the ones in between are still unread",
              !seen(p, 101) && !seen(p, 102) && !seen(p, 103));

        // The count the subject list prints. 101..109 is nine messages, of
        // which 104 and 109 have been read, so seven remain.
        check("the unread count is the truth and not newest minus the mark",
              unreadUpTo(p, 109) == 7);
    }

    // ---- the window self-drains, which is the load-bearing property -----
    {
        Ptr p;
        p.mark = 99;
        markSeen(p, 104);                       // read ahead, out of order
        markSeen(p, 109);
        for (uint32_t n = 100; n <= 103; ++n) markSeen(p, n);
        check("catching up to a read-ahead message drains it",
              p.mark == 104);
        for (uint32_t n = 105; n <= 108; ++n) markSeen(p, n);
        check("and filling the last gap carries the mark through to 109",
              p.mark == 109);
        bool clean = true;
        for (uint8_t k = 0; k < kWindowBytes; ++k) if (p.win[k]) clean = false;
        check("the window is empty again afterwards", clean);
    }

    // ---- the boundary, where an off-by-one would live -------------------
    {
        Ptr p;
        p.mark = 10;
        markSeen(p, 10 + kWindowBits);          // the last bit that fits
        check("the highest message the window can hold is remembered",
              seen(p, 10 + kWindowBits));
        markSeen(p, 10 + kWindowBits + 1);      // one past it
        check("one past the window is not remembered",
              !seen(p, 10 + kWindowBits + 1));
    }

    // ---- the failure DIRECTION, which is the design decision ------------
    {
        Ptr p;
        p.mark = 0;
        markSeen(p, 5000);                      // far beyond the window
        check("a message far ahead is forgotten rather than recorded",
              !seen(p, 5000));
        check("and forgetting it did NOT mark anything else read",
              !seen(p, 1) && !seen(p, 2) && p.mark == 0);
        // The whole point: the error shows the message again. It never hides
        // one. unreadUpTo must therefore count it.
        check("an unread message is never hidden by the window overflowing",
              unreadUpTo(p, 5000) == 5000);
    }

    // ---- nothing read at all --------------------------------------------
    {
        Ptr p;
        check("a fresh caller has read nothing", !seen(p, 1) && !seen(p, 99));
        check("and everything counts as unread", unreadUpTo(p, 12) == 12);
        check("a forum with no messages has no unread", unreadUpTo(p, 0) == 0);
    }

    // ---- record 0 is the header, never a message ------------------------
    {
        Ptr p;
        check("message 0 is never unread, it is the header", seen(p, 0));
        markSeen(p, 0);
        check("and marking it does nothing", p.mark == 0);
    }

    // ---- idempotence: reading the same message twice ---------------------
    {
        Ptr p;
        p.mark = 4;
        markSeen(p, 6);
        markSeen(p, 6);
        markSeen(p, 6);
        check("marking one message repeatedly is stable",
              p.mark == 4 && seen(p, 6) && !seen(p, 5));
        check("and the count does not drift", unreadUpTo(p, 6) == 1);
    }

    // ---- reading backwards, which people actually do --------------------
    {
        Ptr p;
        p.mark = 0;
        for (uint32_t n = 20; n >= 1; --n) markSeen(p, n);
        check("reading 20 down to 1 ends with the mark at 20", p.mark == 20);
        check("and nothing left unread", unreadUpTo(p, 20) == 0);
    }

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
