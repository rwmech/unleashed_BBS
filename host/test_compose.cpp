// Checks for the shared message composer, with no board in it.
//
// This is the code every subsystem that takes a body now runs: forum posts,
// mail, and the feedback system when it exists. A bug here is a bug in all
// of them at once, which is the cost of sharing it and the reason it gets
// tested on its own rather than only through whichever subsystem happened to
// be built first.
//
// The cases below are the ones that were wrong in the forums version before
// it was extracted: a word ending exactly on the margin, a word wider than
// the whole line, a blank line between paragraphs, and a body that fills.
#include <cstdio>
#include <cstring>
#include "../src/core/compose.h"

using namespace compose;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

int main() {
    printf("compose\n");

    // ---- wrapPoint ------------------------------------------------------
    {
        char carry[80];
        const char* line = "the quick brown fox jumps over the lazy dog";
        uint8_t len = static_cast<uint8_t>(strlen(line));
        uint8_t keep = wrapPoint(line, len, carry, sizeof(carry));
        check("a full line breaks at the last space",
              keep == 39 && !strcmp(carry, "dog"));
        check("and the break never lands mid-word",
              line[keep] == ' ');
    }
    {
        // The case that has no answer: one word wider than the line. Letting
        // it stand is ugly; looking for a space that is not there loops.
        char carry[80];
        const char* line = "supercalifragilisticexpialidocious";
        uint8_t len = static_cast<uint8_t>(strlen(line));
        uint8_t keep = wrapPoint(line, len, carry, sizeof(carry));
        check("a word wider than the line is let through whole",
              keep == len && carry[0] == '\0');
    }
    {
        char carry[80];
        uint8_t keep = wrapPoint("", 0, carry, sizeof(carry));
        check("an empty line wraps to nothing", keep == 0 && carry[0] == '\0');
        keep = wrapPoint(nullptr, 0, carry, sizeof(carry));
        check("and a null line does not crash", keep == 0);
    }
    {
        // A trailing space at the break must not be carried, or every
        // wrapped line would start with one.
        char carry[80];
        const char* line = "hello world";
        uint8_t keep = wrapPoint(line, 11, carry, sizeof(carry));
        check("the space at the break is dropped, not carried",
              !strcmp(carry, "world") && keep == 5);
    }
    {
        // Rob, 0.22.2, forum message #14: the wrap broke at the space inside
        // "@BLINK:Special Effects@", the two halves were stored as two lines,
        // and both printed as typed. The effect goes down whole instead.
        char carry[80];
        const char* line = "for some @BLINK:Special Eff";
        uint8_t keep = wrapPoint(line, static_cast<uint8_t>(strlen(line)), carry, sizeof(carry));
        check("an effect is carried whole, not broken at its own space",
              keep == 8 && !strcmp(carry, "@BLINK:Special Eff"));

        line = "shine @TYPE:slowly@ and then more words";
        keep = wrapPoint(line, static_cast<uint8_t>(strlen(line)), carry, sizeof(carry));
        check("a closed effect does not stop a later break", !strcmp(carry, "words"));

        line = "mail me@@home now @RED@red@N@ ok";
        keep = wrapPoint(line, static_cast<uint8_t>(strlen(line)), carry, sizeof(carry));
        check("@@ and colour codes do not open anything", !strcmp(carry, "ok"));

        line = "@OOPS:one two three four five six";
        keep = wrapPoint(line, static_cast<uint8_t>(strlen(line)), carry, sizeof(carry));
        check("an effect wider than the line is let through whole",
              keep == strlen(line) && carry[0] == '\0');
    }
    {
        // A carry buffer smaller than the tail must truncate, never overrun.
        char small[4];
        const char* line = "aa bbbbbbbbbb";
        uint8_t keep = wrapPoint(line, 13, small, sizeof(small));
        check("a short carry buffer truncates instead of overrunning",
              strlen(small) <= 3 && keep == 2);
    }

    // ---- Body -----------------------------------------------------------
    {
        char buf[64];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 8);
        check("a fresh body is empty", b.len == 0 && b.rows == 0 && !buf[0]);

        check("a line is added", addLine(b, "one"));
        check("and a second", addLine(b, "two"));
        check("they are joined with a newline", !strcmp(buf, "one\ntwo"));
        check("the row count follows", b.rows == 2);
    }
    {
        // The blank line. It is how somebody separates paragraphs, so it has
        // to survive as a line rather than being swallowed as nothing.
        char buf[64];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 8);
        addLine(b, "para one");
        addLine(b, "");
        addLine(b, "para two");
        check("a blank line between paragraphs survives",
              !strcmp(buf, "para one\n\npara two") && b.rows == 3);
    }
    {
        char buf[64];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 2);
        addLine(b, "one");
        addLine(b, "two");
        check("the row limit is enforced", full(b));
        check("and a line past it is refused", !addLine(b, "three"));
        check("without corrupting what is there", !strcmp(buf, "one\ntwo"));
    }
    {
        // The byte limit, which bites before the row limit on long lines.
        char buf[12];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 20);
        check("a line that fits is taken", addLine(b, "0123456789"));
        check("one that would overflow is refused", !addLine(b, "more"));
        check("and the buffer is intact and terminated",
              !strcmp(buf, "0123456789") && b.len == 10);
    }

    // ---- popLine: backing out of a committed line -----------------------
    {
        char buf[80], back[80];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 8);
        addLine(b, "first");
        addLine(b, "second");
        addLine(b, "third");

        check("the last line comes back", popLine(b, back, sizeof(back)) &&
              !strcmp(back, "third"));
        check("and the body no longer holds it",
              !strcmp(buf, "first\nsecond") && b.rows == 2);
        check("the body does not end on a dangling newline",
              buf[b.len - 1] != '\n');

        check("again", popLine(b, back, sizeof(back)) && !strcmp(back, "second"));
        check("and again, down to the first",
              popLine(b, back, sizeof(back)) && !strcmp(back, "first"));
        check("which leaves the message empty",
              b.len == 0 && b.rows == 0 && !buf[0]);
        check("and there is nothing more to take",
              !popLine(b, back, sizeof(back)));
    }
    {
        // A blank line is a line and must come back like any other, or a
        // caller walking backwards would skip straight over their paragraph
        // break and be unable to remove it.
        char buf[80], back[80];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 8);
        addLine(b, "para");
        addLine(b, "");
        check("a blank line pops back as a blank line",
              popLine(b, back, sizeof(back)) && back[0] == '\0' && b.rows == 1);
        check("leaving the line above intact", !strcmp(buf, "para"));
    }
    {
        // Pop then re-add: the round trip has to be lossless, because that
        // is exactly what editing a line is.
        char buf[80], back[80];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 8);
        addLine(b, "one");
        addLine(b, "two");
        popLine(b, back, sizeof(back));
        addLine(b, back);
        check("popping and re-adding a line changes nothing",
              !strcmp(buf, "one\ntwo") && b.rows == 2);
    }
    {
        char buf[80];
        Body b;
        begin(b, buf, sizeof(buf) - 1, 8);
        check("popping an empty body is refused, not a crash",
              !popLine(b, nullptr, 0));
    }

    // ---- the terminators ------------------------------------------------
    check("/s saves",            isSave("/s") && isSave("/S") && isSave("/save"));
    check("/a aborts",           isAbort("/a") && isAbort("/A") && isAbort("/abort"));
    // Only on a line of its own: somebody writing about a command still can.
    check("a line merely starting with /s is text",
          !isSave("/s is how you save") && !isAbort("/a means abort"));
    check("an ordinary line is neither",
          !isSave("hello") && !isAbort("hello"));
    check("a null line is neither", !isSave(nullptr) && !isAbort(nullptr));

    // ---- lineWidth: the composed line follows the terminal ---------------
    //
    // Both subsystems used to open the editor at 72 whatever the caller was
    // sitting at, so a C64 got a four column line number plus 72 characters
    // against a 40 column screen and every line wrapped in the terminal.
    {
        // A C64: 40 columns, four for "16: ", one held back for the cursor.
        check("40 columns leaves 35 for the text", lineWidth(40, 4, 72) == 35);

        // A wide terminal is capped by the editor's real buffer, not by the
        // screen. LineEditor holds BBS_LINE_MAX and silently clamps a larger
        // request, which is how the 72 character body went unnoticed.
        check("80 columns is capped at the buffer", lineWidth(80, 4, 72) == 72);
        check("132 columns is capped too",          lineWidth(132, 4, 72) == 72);

        // Exactly at the boundary: 77 columns is 72 usable with a 4 column
        // prompt and one held back, so this is the widest screen that is not
        // yet capped.
        check("77 columns lands exactly on the cap", lineWidth(77, 4, 72) == 72);
        check("76 columns is one short",             lineWidth(76, 4, 72) == 71);

        // An unknown width counts as 40, the rule the rest of the board
        // follows: a terminal that never said how wide it is has promised
        // nothing, and a short line beats a wrapped one.
        check("unknown width is treated as 40", lineWidth(0, 4, 72) == 35);
        check("an absurdly narrow width too",   lineWidth(12, 4, 72) == 35);

        // No prompt means the whole width bar the cursor column.
        check("no prompt uses the full width", lineWidth(40, 0, 72) == 39);

        // A prompt that swallows the screen must still leave something to
        // type into rather than returning zero and refusing every keystroke.
        check("a prompt wider than the screen still leaves room",
              lineWidth(40, 60, 72) >= 1);
    }

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
