// Checks for the names of the backups on the SD card and the rule that
// prunes the nightly ones (1.1.0), with no card in them.
//
// The one that matters most is the last group: "keep the last 7" must only
// ever count and remove nightly-YYYYMMDD.zip. A sysop's own backup made
// before a risky change, or a screens zip copied over from a laptop, is
// never the oldest nightly, however the names sort.
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "../src/core/cardnames.h"

using namespace cardbak;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

// The prune loop the board runs, over a folder held in a vector: scan,
// remove the oldest while there are too many, scan again.
static std::vector<std::string> prune(std::vector<std::string> folder, std::vector<std::string>* removed) {
    for (;;) {
        NightlyScan scan;
        for (const auto& n : folder) scan.feed(n.c_str());
        const char* victim = scan.prune();
        if (!victim) break;
        std::string v = victim;
        if (removed) removed->push_back(v);
        for (auto it = folder.begin(); it != folder.end(); ++it)
            if (*it == v) { folder.erase(it); break; }
    }
    return folder;
}

int main() {
    printf("Backup names on the card\n");

    char out[40];
    check("a full backup's name", makeName(Kind::Full, "20260923-2210", out, sizeof(out)) &&
                                   !strcmp(out, "unleashed-20260923-2210.zip") && strlen(out) == 27);
    check("a screens backup's name", makeName(Kind::Screens, "20260922-0900", out, sizeof(out)) &&
                                      !strcmp(out, "screens-20260922-0900.zip") && strlen(out) == 25);
    check("a nightly one keeps only the date", makeName(Kind::Nightly, "20260923-0300", out, sizeof(out)) &&
                                                !strcmp(out, "nightly-20260923.zip") && strlen(out) == 20);
    check("no name from a clock that is not there",
          !makeName(Kind::Full, "--", out, sizeof(out)) && !out[0]);
    check("nor from a stamp of the wrong shape",
          !makeName(Kind::Full, "2026-09-23 22", out, sizeof(out)) &&
          !makeName(Kind::Full, "20260923x2210", out, sizeof(out)));
    check("nor into a buffer too small for it, which is left empty",
          !makeName(Kind::Full, "20260923-2210", out, 20) && !out[0]);
    check("an Other has no name of the board's own", !makeName(Kind::Other, "20260923-2210", out, sizeof(out)));

    check("the three names read back as what they are",
          kindOf("unleashed-20260923-2210.zip") == Kind::Full &&
          kindOf("screens-20260922-0900.zip") == Kind::Screens &&
          kindOf("nightly-20260923.zip") == Kind::Nightly);
    check("a zip of somebody's own is listed, as Other",
          kindOf("theme.zip") == Kind::Other && kindOf("C64 SCREENS.ZIP") == Kind::Other);
    check("a name that only looks like a nightly is Other",
          kindOf("nightly-2026092.zip") == Kind::Other && kindOf("nightly-2026092x.zip") == Kind::Other &&
          kindOf("Nightly-20260923.zip") == Kind::Other && kindOf("nightly-20260923.ZIP") == Kind::Other);
    check("not a zip is not listed", kindOf("notes.txt") == Kind::None && !listable("backup"));
    check("a hidden file is not listed", !listable(".staging.zip"));
    check("nor a path, however it is written",
          !listable("../users.zip") && !listable("a/b.zip") && !listable("a\\b.zip") && !listable("c:x.zip"));
    check("nor a name too long for the list's column",
          !listable("a-name-far-too-long-for-it-x.zip") && listable("a-name-just-long-enough.zip"));
    check("a laptop's name with a space in it is listed", listable("my backup.zip"));
    check("but not one opening with a space, or with a control character",
          !listable(" backup.zip") && !listable("x\tz.zip"));

    printf("Pruning the nightly backups\n");

    std::vector<std::string> folder;
    for (int d = 1; d <= 7; ++d) {
        char n[32];
        snprintf(n, sizeof(n), "nightly-202609%02d.zip", d);
        folder.push_back(n);
    }
    std::vector<std::string> removed;
    auto left = prune(folder, &removed);
    check("seven nightly zips: nothing to remove", removed.empty() && left.size() == 7);

    folder.push_back("nightly-20260908.zip");
    removed.clear();
    left = prune(folder, &removed);
    check("an eighth removes exactly the oldest",
          removed.size() == 1 && removed[0] == "nightly-20260901.zip" && left.size() == 7);

    // The sysop's own backups sort before the nightly ones ("screens-" and
    // most of all a hand-named zip), and a full backup is older by date.
    // None of them may be taken, however many there are.
    std::vector<std::string> mixed = {
        "unleashed-20250101-0000.zip", "screens-20240101-0000.zip", "aaa.zip",
        "nightly-0000000.zip", "nightly-2026091.zip", "Nightly-20200101.zip",
    };
    for (int d = 10; d <= 20; ++d) {
        char n[32];
        snprintf(n, sizeof(n), "nightly-202609%02d.zip", d);
        mixed.push_back(n);
    }
    removed.clear();
    left = prune(mixed, &removed);
    bool onlyNightly = true;
    for (const auto& r : removed) if (kindOf(r.c_str()) != Kind::Nightly) onlyNightly = false;
    check("eleven nightly zips among the sysop's own: four removed", removed.size() == 4);
    check("and every one removed was a nightly zip", onlyNightly);
    check("the oldest four, in order",
          removed.size() == 4 && removed[0] == "nightly-20260910.zip" && removed[3] == "nightly-20260913.zip");
    bool ownKept = true;
    for (const char* own : { "unleashed-20250101-0000.zip", "screens-20240101-0000.zip", "aaa.zip",
                             "nightly-0000000.zip", "nightly-2026091.zip", "Nightly-20200101.zip" }) {
        bool found = false;
        for (const auto& n : left) if (n == own) found = true;
        if (!found) ownKept = false;
    }
    check("the sysop's own backups, and the look-alikes, all stay", ownKept);

    NightlyScan none;
    none.feed("unleashed-20260923-2210.zip");
    check("a folder with no nightly zips prunes nothing", none.count == 0 && !none.prune());

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
