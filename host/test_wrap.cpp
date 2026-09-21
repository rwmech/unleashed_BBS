// Standalone checks for bbsu::wrap, compiled without the rest of the board.
//
// Word wrap is the kind of thing that looks obviously right and is wrong at
// the margins: the word that ends exactly on the boundary, the word wider
// than the row, the run of spaces, the empty string. Those are cheap to test
// here and expensive to find on a C64.
#include <cstdio>
#include <cstring>
#include <cstdint>

// The real header, not a copy. A test that carries its own copy of the
// function under test is a test that keeps passing after the original
// changes, which is worse than having no test at all.
#include "../src/core/bbs_util.h"

static int fails = 0;
static int passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

// Collect every line wrap produces, joined with '|' so one string shows the
// whole layout and a wrong break is obvious at a glance.
static void layout(const char* src, uint8_t cols, char* out, size_t outN) {
    char line[128];
    out[0] = '\0';
    const char* p = src;
    int guard = 0;
    while ((p = bbsu::wrap(p, line, sizeof(line), cols)) != nullptr && ++guard < 40) {
        if (out[0]) strncat(out, "|", outN - strlen(out) - 1);
        strncat(out, line, outN - strlen(out) - 1);
        if (!*p) break;
    }
}

int main() {
    char got[512];
    char line[128];

    printf("bbsu::wrap\n");

    layout("the quick brown fox jumps over the lazy dog", 16, got, sizeof(got));
    check("breaks on spaces, never mid-word",
          strcmp(got, "the quick brown|fox jumps over|the lazy dog") == 0);
    if (fails) printf("       got: [%s]\n", got);

    // The case that catches an off-by-one: a word ending exactly on the
    // margin must not be broken and must not drag the next word up.
    layout("abcd efgh ijkl", 9, got, sizeof(got));
    check("a word ending exactly on the margin stays whole",
          strcmp(got, "abcd efgh|ijkl") == 0);
    if (fails) printf("       got: [%s]\n", got);

    // A word wider than the row has to go somewhere. Breaking it is ugly;
    // letting it overflow wraps in the terminal and, on a refresh screen,
    // leaves a tail behind on every redraw.
    layout("supercalifragilistic x", 8, got, sizeof(got));
    check("a word wider than the row is broken, not overflowed",
          strncmp(got, "supercal", 8) == 0 && strstr(got, "|") != nullptr);
    if (fails) printf("       got: [%s]\n", got);

    // No line may ever start with a space, or the left margin looks ragged
    // for reasons the reader cannot see.
    layout("one    two", 5, got, sizeof(got));
    check("runs of spaces do not push a line off the margin",
          got[0] != ' ' && strstr(got, "|t") != nullptr);
    if (fails) printf("       got: [%s]\n", got);

    layout("first\nsecond", 40, got, sizeof(got));
    check("an explicit newline breaks the line even when it fits",
          strcmp(got, "first|second") == 0);
    if (fails) printf("       got: [%s]\n", got);

    check("an empty string produces nothing",
          bbsu::wrap("", line, sizeof(line), 40) == nullptr);
    check("a null source produces nothing",
          bbsu::wrap(nullptr, line, sizeof(line), 40) == nullptr);
    check("all spaces produce nothing",
          bbsu::wrap("     ", line, sizeof(line), 40) == nullptr);

    // cols 0 would be a divide-by-nothing or an infinite loop in a naive
    // implementation. A NAWS negotiation carrying zero really does reach the
    // board, which is why this is tested rather than assumed impossible.
    layout("ab cd", 0, got, sizeof(got));
    check("zero columns terminates instead of looping", got[0] != '\0');

    // The output buffer, not the column count, is the real bound when it is
    // smaller. Overrunning it would be a stack smash.
    const char* rest = bbsu::wrap("aaaaaaaaaaaaaaaaaaaa", line, 8, 40);
    check("a short output buffer bounds the line, not the column count",
          strlen(line) <= 7 && rest != nullptr);

    // A single line that fits entirely should come back whole, with the
    // return pointing at the terminator so the caller stops.
    rest = bbsu::wrap("short", line, sizeof(line), 40);
    check("text that fits comes back whole",
          strcmp(line, "short") == 0 && rest && *rest == '\0');

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
