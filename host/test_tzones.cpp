// Checks for the timezone picker's table (1.1.0), with no board in it.
//
// The table is one list turned into two things: the rows CONFIG looks a TZ
// string up in, and the '|' separated choices the form's cycle steps
// through. Each rule below is one a reading of the table would miss and a
// sysop would meet: a name longer than the box, a string longer than the key
// takes, two zones sharing a string (then "the file's value opens as that
// entry" has two answers), a '|' in a name (the cycle would split it in two),
// or the choices and the rows falling out of step.
//
// The strings themselves were checked against the tz database's compiled
// zones by the copywriter (internal/copy-1.1.0-2026-09-23.md, "How the
// strings were checked"). This checks the board's copy of them is intact.
#include <cstdio>
#include <cstring>
#include "../src/core/tzones.h"

using namespace tzones;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

int main() {
    printf("Timezone table\n");

    check("34 zones, as the copy's table has", kZoneCount == 34);
    check("UTC first, what every board ships with",
          !strcmp(kZones[0].name, "UTC") && !strcmp(kZones[0].posix, "UTC0"));

    bool namesFit = true, stringsFit = true, noBar = true, printable = true;
    for (const Zone& z : kZones) {
        if (strlen(z.name) > kNameMax || !z.name[0])   namesFit = false;
        if (strlen(z.posix) > kPosixMax || !z.posix[0]) stringsFit = false;
        if (strchr(z.name, '|'))                        noBar = false;
        for (const char* p = z.name; *p; ++p)
            if (*p < 0x20 || *p > 0x7E || strchr("{}~\\_^`", *p)) printable = false;
    }
    check("every name fits the 24 the box and the copy allow", namesFit);
    check("every string fits the 40 the tz key takes", stringsFit);
    check("no name holds the cycle's separator", noBar);
    check("every name is plain ASCII a C64 can show", printable);

    bool uniqueStrings = true, uniqueNames = true;
    for (uint8_t i = 0; i < kZoneCount; ++i)
        for (uint8_t k = static_cast<uint8_t>(i + 1); k < kZoneCount; ++k) {
            if (!strcmp(kZones[i].posix, kZones[k].posix)) uniqueStrings = false;
            if (!strcmp(kZones[i].name, kZones[k].name))   uniqueNames = false;
        }
    check("no two zones share a string, so a file value has one answer", uniqueStrings);
    check("and no two share a name", uniqueNames);

    // The choices: the names in order, then Custom, and nothing else.
    {
        char buf[sizeof(kZoneChoices)];
        memcpy(buf, kZoneChoices, sizeof(buf));
        uint8_t i = 0;
        bool inStep = true;
        const char* last = nullptr;
        for (char* p = strtok(buf, "|"); p; p = strtok(nullptr, "|")) {
            if (i < kZoneCount && strcmp(p, kZones[i].name)) inStep = false;
            last = p;
            ++i;
        }
        check("the cycle is the table's names in the table's order", inStep);
        check("one more choice than zones", i == kZoneCount + 1);
        check("and the last one is Custom", last && !strcmp(last, kCustom));
    }

    // The round trip CONFIG makes: a file's string opens as its name, and
    // picking that name writes the same string back.
    bool round = true;
    for (const Zone& z : kZones) {
        const char* n = nameFor(z.posix);
        const char* p = n ? posixFor(n) : nullptr;
        if (!n || strcmp(n, z.name) || !p || strcmp(p, z.posix)) round = false;
    }
    check("every string opens as its zone and writes back unchanged", round);
    check("the US Central example the docs use",
          !strcmp(nameFor("CST6CDT,M3.2.0,M11.1.0"), "US Central (Chicago)"));
    check("a string the table does not have opens as Custom (nullptr)",
          nameFor("CET-1CEST,M3.5.0,M10.5.0/2") == nullptr);
    check("a string one space off is not quietly tidied into a zone",
          nameFor("UTC0 ") == nullptr && nameFor(" UTC0") == nullptr);
    check("an empty string is no zone", nameFor("") == nullptr && nameFor(nullptr) == nullptr);
    check("Custom has no string behind it", posixFor(kCustom) == nullptr);
    check("nor does a name the table lacks", posixFor("Mars (Olympus Mons)") == nullptr);

    // valid (1.1.1, TZ-bad): what newlib's tzset can read. Every string in
    // the table first, since CONFIG writes them without asking.
    printf("Timezone strings the board can read\n");
    bool tableOk = true;
    for (const Zone& z : kZones)
        if (!valid(z.posix)) { tableOk = false; printf("        refused: %s\n", z.posix); }
    check("every zone in the table", tableOk);
    const char* const good[] = {
        "UTC0", "UTC+0", "GMT0", "EST5", "EST5EDT", "EST5EDT,M3.2.0,M11.1.0",
        "EST5EDT,M3.2.0/2,M11.1.0/2:00:00", "EST5EDT4,M3.2.0,M11.1.0",
        "EST5EDT,M3.2.0",                     // newlib fills in the second rule
        "CST6CDT,J60,J300", "CST6CDT,59,299", "NST3:30NDT,M3.2.0,M11.1.0",
        "<+0530>-5:30", "<-03>3<-02>,M3.5.0/-2,M10.5.0/-1", "IST-5:30:00",
        ":UTC0", "ABCDEFGHIJ5",               // ten letters, newlib's limit
    };
    bool goodOk = true;
    for (const char* g : good)
        if (!valid(g)) { goodOk = false; printf("        refused: %s\n", g); }
    check("strings newlib reads, taken", goodOk);
    const char* const bad[] = {
        "", "UTC", "EST", "5", "EST+", "EST 5", "EST5EDT,M13.2.0,M11.1.0",
        "EST5EDT,M3.6.0,M11.1.0", "EST5EDT,M3.2.7,M11.1.0", "EST5EDT,M3.2,M11.1.0",
        "EST5EDT,J0,J300", "EST5EDT,366,1", "EST5EDT,M3.2.0/,M11.1.0",
        "EST5EDT,M3.2.0,M11.1.0x", "EST5EDT,M3.2.0,M11.1.0,M1.1.0", "<+05", "<>5",
        "<+0530-5:30", "ABCDEFGHIJK5", "EST25", "EST5:60", "EST5EDT,M3.2.0/168,M11.1.0",
        "CST6CDT,M3.2.0,M11.1.0 ", "America/Chicago",
    };
    bool badOk = true;
    for (const char* b : bad)
        if (valid(b)) { badOk = false; printf("        taken: \"%s\"\n", b); }
    check("strings it would run as unnamed UTC, and typos, refused", badOk);
    check("no string at all is no TZ", !valid(nullptr));

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
