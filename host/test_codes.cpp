// Checks for the inline @-codes, with no board, card or session in it.
//
// Two things are worth a unit test here and neither can be seen by reading
// the code. The wrap has to measure what is SHOWN, not what was typed, or a
// coloured sentence wraps early by the width of its codes; and it must
// never break inside a code, or "@BLINK:hello world@" splits at its own
// space and prints as two halves of something. And the limit has to be
// counted the same way by the wrap and by the row, or a row that the wrap
// measured as fitting overflows when the ninth code prints as typed.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include "../src/core/codes.h"
#include "../src/core/sysconfig.h"
#include "../src/core/clock.h"
#include "../src/platform/platform.h"

// The three things codes.cpp and fx.cpp reach outside themselves for.
// Stubbed rather than linked, so this stays a test of the codes and not of
// the configuration loader, the clock or the platform layer.
namespace syscfg { const SysConfig& get() { static SysConfig c; return c; } }
namespace clk {
size_t fmt(char* buf, size_t n, const char* f) {
    const char* v = (f[1] == 'H') ? "21:14" : "22 Sep 2026";
    snprintf(buf, n, "%s", v);
    return strlen(buf);
}
}
namespace plat { uint32_t random32() { return 4; } }

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

// rows: every row wrap() produces, as strings.
static std::string rows(const char* src, uint8_t cols) {
    codes::Painter p;
    char line[160];
    std::string all;
    const char* s = src;
    while ((s = codes::wrap(s, line, sizeof(line), cols, p)) != nullptr) {
        all += line;
        all += '|';
        if (!*s) break;
    }
    return all;
}

// shown: what a plain ASCII terminal ends up displaying for one row.
struct Collect { std::string out; };
static int sink(void* ctx, const uint8_t* d, size_t n) {
    static_cast<Collect*>(ctx)->out.append(reinterpret_cast<const char*>(d), n);
    return static_cast<int>(n);
}
static std::string shown(const char* text, uint8_t cols, codes::Painter& p) {
    Term t;
    t.setType(TermType::Ascii, Charset::Ascii, 80, 24);
    static Timeline tl;
    tl.clear();
    codes::row(t, tl, text, cols, p);
    Collect c;
    for (uint32_t now = 1; !tl.empty() && now < 100000; now += 50) tl.pump(now, sink, &c);
    return c.out;
}

int main() {
    printf("inline codes\n");

    // ---- the wrap measures what is shown --------------------------------
    // Twenty characters of words with two colour codes in them. At twenty
    // columns it is one row; measured by bytes it was two.
    check("colour codes take no width",
          rows("@RED@abcde fghij@N@ klmno pqrst", 23) == "@RED@abcde fghij@N@ klmno pqrst|");
    check("an effect is as wide as its text",
          rows("aa @BLINK:bbbb@ cc", 10) == "aa @BLINK:bbbb@ cc|");
    check("and is never split at its own space",
          rows("xx @BLINK:hello world@ yy", 12) == "xx|@BLINK:hello world@|yy|");
    check("a code wider than the row gets a row of its own",
          rows("@TYPE:abcdefghijkl@", 8) == "@TYPE:abcdefghijkl@|");
    check("@RULE@ ends its row",
          rows("ab @RULE@ cd", 20) == "ab @RULE@|cd|");
    check("an explicit break still wins",
          rows("one\ntwo", 20) == "one|two|");

    // ---- what is not a code prints as typed -----------------------------
    char out[128];
    codes::plain("mail me@example.com or @work@", out, sizeof(out));
    check("an email address is not a code", strcmp(out, "mail me@example.com or @work@") == 0);
    codes::plain("@CLS@@DELAY:900@@BAUD:300@@USER@", out, sizeof(out));
    check("CLS, DELAY, BAUD and USER are not caller codes",
          strcmp(out, "@CLS@@DELAY:900@@BAUD:300@@USER@") == 0);
    codes::plain("@BLACK@hidden", out, sizeof(out));
    check("black is not offered", strcmp(out, "@BLACK@hidden") == 0);
    codes::plain("@@ and @@@", out, sizeof(out));
    check("@@ is a literal @", strcmp(out, "@ and @@") == 0);

    // ---- plain keeps the words and drops the decoration -----------------
    codes::plain("@RED@hi @BLINK:there@ @OOPS:idiot@@BELL@!", out, sizeof(out));
    check("plain keeps an effect's words and drops @OOPS@'s",
          strcmp(out, "hi there !") == 0);

    // ---- the limit ------------------------------------------------------
    {
        codes::Painter p;
        p.begin(Color::Grey, true);
        std::string s = shown("@RED@@RED@@RED@@RED@@RED@@RED@@RED@@RED@@RED@x", 80, p);
        check("the ninth code prints as typed", s.find("@RED@x") != std::string::npos);
        check("and the first eight print nothing on plain ASCII",
              s.find("@RED@@RED@") == std::string::npos);
    }
    {
        // The wrap must count the limit exactly as row() does, across rows.
        // Nine codes, the ninth over the limit and so as wide as it was
        // typed. Wrapped and painted row by row, the ninth lands on the
        // second row as five columns of text and "d" has to go down; a
        // wrap that forgot the first row's eight would call it a code, fit
        // "@RED@cd" in six columns and the row would overflow.
        codes::Painter p;
        p.begin(Color::Grey, true);
        char line[160];
        std::string all;
        const char* s = "@RED@@RED@@RED@@RED@@RED@@RED@@RED@@RED@ab @RED@cd";
        while ((s = codes::wrap(s, line, sizeof(line), 6, p)) != nullptr) {
            all += line;
            all += '|';
            shown(line, 6, p);                    // paints, and so counts
            if (!*s) break;
        }
        check("the wrap counts the limit the same way the row does",
              all == "@RED@@RED@@RED@@RED@@RED@@RED@@RED@@RED@ab|@RED@c|d|");
    }

    // ---- fill-ins and a bell that rings once ----------------------------
    {
        codes::Painter p;
        p.begin(Color::Grey, true);
        std::string s = shown("@TIME@ @BELL@@BELL@", 80, p);
        check("@TIME@ prints the time", s.find("21:14") != std::string::npos);
        check("@BELL@ rings once a message",
              std::count(s.begin(), s.end(), '\a') == 1);
    }
    {
        codes::Painter p;
        p.begin(Color::Grey, false);
        std::string s = shown("@BELL@", 80, p);
        check("and not at all for a reader with the bell off",
              s.find('\a') == std::string::npos);
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
