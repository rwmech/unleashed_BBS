// Checks for the BOOT-hold reset's timing and the Wi-Fi fallback's, with no
// board, button, LED or radio in them (1.1.0).
//
// Every rule here lives at a boundary: 7, 15 and 20 seconds, a release that
// is really a bounce, a press a moment after the window closed, sixty seconds
// to the millisecond. None of that shows in a reading of the code and all of
// it shows on a bench as the wrong thing happening to somebody's accounts, so
// each boundary is driven from both sides on a simulated clock.
//
// The files, the console lines and the restart are tested end to end by
// tools/testclient.py (test_boot_hold, test_config_wifi_fallback), through
// the host build's simulated clock in main_host.cpp.
#include <cstdio>
#include "../src/core/recovery.h"

using namespace recovery;

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

// A hold of holdMs starting at pressAt, polled every stepMs, and let go for
// good. Returns the release event and counts the stage events on the way.
struct Run {
    BootHold::Event rel{ BootHold::Ev::None, Stage::Waiting, 0 };
    int  stages = 0;
    bool held   = false;
    bool closed = false;
};

static Run hold(uint32_t pressAt, uint32_t holdMs, uint32_t stepMs = 10) {
    BootHold h;
    Run r;
    for (uint32_t t = 0; t < pressAt + holdMs + 2000 && h.watching(); t += stepMs) {
        bool down = t >= pressAt && t < pressAt + holdMs;
        BootHold::Event e = h.feed(t, down);
        if (e.ev == BootHold::Ev::Held)     r.held = true;
        if (e.ev == BootHold::Ev::Stage)    ++r.stages;
        if (e.ev == BootHold::Ev::Released) r.rel = e;
        if (e.ev == BootHold::Ev::Closed)   r.closed = true;
    }
    return r;
}

int main() {
    printf("BOOT-hold reset\n");

    // ---- the bands, from both sides of each edge -----------------------
    {
        Run r = hold(500, 6900);
        check("6.9 s is nothing", r.held && r.rel.ev == BootHold::Ev::Released &&
                                  r.rel.stage == Stage::Counting && r.stages == 0);
        check("and says it was let go at 6 s", r.rel.heldMs / 1000 == 6);
    }
    {
        Run r = hold(500, 7100);
        check("7.1 s is the password", r.rel.stage == Stage::Password && r.stages == 1);
    }
    {
        Run r = hold(500, 14900);
        check("14.9 s is still the password", r.rel.stage == Stage::Password);
    }
    {
        Run r = hold(500, 15100);
        check("15.1 s is the factory reset", r.rel.stage == Stage::Factory && r.stages == 2);
    }
    {
        Run r = hold(500, 19900);
        check("19.9 s is still the factory reset", r.rel.stage == Stage::Factory);
    }
    {
        Run r = hold(500, 20100);
        check("20.1 s is abandoned", r.rel.stage == Stage::Abort && r.stages == 3);
    }
    {
        Run r = hold(500, 45000);
        check("and so is a minute: nothing on release", r.rel.stage == Stage::Abort);
    }

    // ---- what happens is what was shown --------------------------------
    // A pass that runs long must not skip a stage: each is said in turn, and
    // the release acts on the last one said, never on one the LED and the
    // console had not reached.
    {
        Run r = hold(500, 16000, 900);
        check("polled every 900 ms, 16 s still reaches the factory stage", r.rel.stage == Stage::Factory);
        check("having said the 7 s stage on the way", r.stages == 2);
    }
    {
        // Let go at 7.05 s, but the only look while it was down that could
        // have seen 7 s was at 6.4 s: it had not been said, so nothing.
        BootHold h;
        h.feed(0, true);
        h.feed(6400, true);
        h.feed(7050, false);
        BootHold::Event e = h.feed(7200, false);
        check("a release the board had not yet shown as 7 s does nothing",
              e.ev == BootHold::Ev::Released && e.stage == Stage::Counting);
        check("and says a time that agrees: the last it saw held, 6 s", e.heldMs / 1000 == 6);
    }

    // ---- bounces are not releases --------------------------------------
    {
        BootHold h;
        Stage end = Stage::Waiting;
        for (uint32_t t = 0; t < 12000 && h.watching(); t += 10) {
            // held from 500 for 9 s, with a 30 ms blip at 3 s and one at 8 s
            bool down = t >= 500 && t < 9500 && !(t >= 3000 && t < 3030) && !(t >= 8000 && t < 8030);
            BootHold::Event e = h.feed(t, down);
            if (e.ev == BootHold::Ev::Released) end = e.stage;
        }
        check("a 30 ms bounce part way through is not a release", end == Stage::Password);
    }
    {
        BootHold h;
        h.feed(0, true);
        h.feed(8000, true);
        h.feed(8010, false);
        BootHold::Event e = h.feed(8100, false);
        check("up for 90 ms is not yet a release", e.ev == BootHold::Ev::None && h.watching());
        e = h.feed(8110, false);
        check("up for 100 ms is", e.ev == BootHold::Ev::Released);
        check("and the time held is to the last look that saw it down", e.heldMs == 8000);
    }

    // ---- the window -----------------------------------------------------
    {
        Run r = hold(9900, 8000);
        check("a press at 9.9 s counts", r.held && r.rel.stage == Stage::Password);
    }
    {
        Run r = hold(10050, 8000);
        check("a press at 10.05 s does not", !r.held && r.closed);
    }
    {
        BootHold h;
        for (uint32_t t = 0; t <= kWindowMs && h.watching(); t += 10) h.feed(t, false);
        check("nobody pressing it: the watch ends when the window closes", !h.watching());
        BootHold::Event e = h.feed(kWindowMs + 5000, true);
        check("and a press after that is the backup button's, not the watch's",
              e.ev == BootHold::Ev::None && !h.watching());
    }

    // ---- the LED --------------------------------------------------------
    {
        BootHold h;
        h.feed(0, false);
        check("nothing held: the LED is traffic's", h.led() == Led::Free);
        h.feed(1000, true);                         // pressed at 1 s
        check("held: a slow blink", h.led() == Led::Slow);
        check("on for the first half second", h.ledOn(1000) && h.ledOn(1499));
        check("off for the second", !h.ledOn(1500) && !h.ledOn(1999));
        check("and on again", h.ledOn(2000));
        h.feed(8000, true);                         // 7 s held
        check("from 7 s it flashes rapidly", h.led() == Led::Fast);
        check("100 ms on, 100 ms off", h.ledOn(8000) && !h.ledOn(8100) && h.ledOn(8200));
        h.feed(16000, true);                        // 15 s held
        check("from 15 s it is solid", h.led() == Led::Solid && h.ledOn(16000) && h.ledOn(16150));
        h.feed(21000, true);                        // 20 s held
        check("at 20 s it goes off", h.led() == Led::Off && !h.ledOn(21000) && !h.ledOn(21500));
        h.feed(22000, false);
        h.feed(22200, false);
        check("and once let go it is traffic's again", h.led() == Led::Free && !h.watching());
    }

    // ---- words -----------------------------------------------------------
    {
        const char* pw = noteText(NOTE_PASSWORD);
        const char* fr = noteText(NOTE_FACTORY);
        check("each reset has words and a plain restart has none",
              pw && fr && !noteText(NOTE_NONE) && !noteText(99));
        // "Last restart: <text>." on one 40 column line, and inside the 32
        // byte boot reason it is copied into.
        size_t a = 0, b = 0;
        while (pw && pw[a]) ++a;
        while (fr && fr[b]) ++b;
        check("short enough for a C64 and for bootReason_", a + 15 <= 39 && b + 15 <= 39 && a < 32 && b < 32);
    }

    printf("Wi-Fi fallback\n");
    {
        WifiFallback f;
        f.begin(1000, true);
        check("armed at boot", f.armed());
        check("not due at 59.9 s", !f.due(1000 + 59900, false, false));
        check("due at 60 s", f.due(1000 + 60000, false, false));
        check("once", !f.due(1000 + 60100, false, false) && !f.armed());
    }
    {
        WifiFallback f;
        f.begin(0, true);
        check("a join disarms it", !f.due(30000, true, false) && !f.armed());
        check("so it never comes due", !f.due(90000, false, false));
    }
    {
        WifiFallback f;
        f.begin(0, false);
        check("no last good network: never due", !f.due(90000, false, false));
    }
    {
        WifiFallback f;
        f.begin(0, true);
        check("waits while Improv has the radio", !f.due(61000, false, true) && f.armed());
        check("and goes when it is done", f.due(62000, false, false));
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
