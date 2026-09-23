// Checks for the Improv Wi-Fi Serial packets, with no UART or radio in it.
//
// The expected bytes are written out by hand from the protocol page rather
// than produced by improv.cpp and compared with itself: a codec tested only
// against its own output agrees with itself, which is the lesson lrzsz and
// forum_check.py both taught here. The checksum in particular is worked on
// paper below, because the spec page never states it and the reference
// implementation is where it was confirmed.
#include <cstdio>
#include <cstring>
#include "../src/core/improv.h"

static int fails = 0, passes = 0;

static void check(const char* what, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passes; else ++fails;
}

using improv::Parser;

// feedAll: every byte through the parser; how many whole packets came out,
// and how many failed their checksum.
static int feedAll(Parser& p, const uint8_t* b, size_t n, int* bad = nullptr) {
    int got = 0;
    for (size_t i = 0; i < n; ++i) {
        Parser::Res r = p.feed(b[i]);
        if (r == Parser::Res::Packet) ++got;
        if (r == Parser::Res::BadChecksum && bad) ++*bad;
    }
    return got;
}

int main() {
    printf("improv\n");

    // "Request current state", as the browser sends it. IMPROV sums to 0x1DD;
    // then version 1, type 3, length 2, command 2, payload length 0. The low
    // byte of 0x1E5 is 0xE5.
    const uint8_t getState[] = { 'I','M','P','R','O','V', 0x01, 0x03, 0x02, 0x02, 0x00, 0xE5 };

    // ---- parsing ---------------------------------------------------------
    {
        Parser p;
        check("a whole packet is found", feedAll(p, getState, sizeof(getState)) == 1);
        check("its type is RPC", p.type() == improv::T_RPC);
        uint8_t cmd = 0, plen = 9;
        const uint8_t* pay = nullptr;
        check("the RPC parses", improv::parseRpc(p.data(), p.len(), cmd, pay, plen));
        check("as request current state", cmd == improv::C_STATE && plen == 0);
    }
    {
        // The log shares this line, so a packet arrives among other text,
        // including a false start that looks like the header for a while.
        uint8_t buf[64];
        const char noise[] = "I (123) wifi: IMPRO IMPIMPROV";
        size_t n = strlen(noise) - 6;                   // everything up to the real "IMPROV"
        memcpy(buf, noise, n);
        memcpy(buf + n, getState, sizeof(getState));
        Parser p;
        check("found after log text and a false start",
              feedAll(p, buf, n + sizeof(getState)) == 1);
    }
    {
        uint8_t bad[sizeof(getState)];
        memcpy(bad, getState, sizeof(bad));
        bad[sizeof(bad) - 1] ^= 0x01;
        Parser p;
        int badSum = 0;
        check("a wrong checksum is not a packet", feedAll(p, bad, sizeof(bad), &badSum) == 0);
        check("and is reported as one", badSum == 1);
        check("the next good packet still parses", feedAll(p, getState, sizeof(getState)) == 1);
    }
    {
        // A length past what the board will buffer is dropped at once, not
        // waited out: waiting for 200 bytes that never come would swallow
        // the next real packet.
        uint8_t big[] = { 'I','M','P','R','O','V', 0x01, 0x03, 200 };
        Parser p;
        feedAll(p, big, sizeof(big));
        check("an oversized length is abandoned", feedAll(p, getState, sizeof(getState)) == 1);
    }
    {
        uint8_t v2[sizeof(getState)];
        memcpy(v2, getState, sizeof(v2));
        v2[6] = 0x02;
        Parser p;
        check("another protocol version is ignored", feedAll(p, v2, sizeof(v2)) == 0);
    }

    // ---- Wi-Fi settings --------------------------------------------------
    {
        // command 1, payload: 4 "MECH", 6 "pa#s w"
        const uint8_t data[] = { 0x01, 12, 4, 'M','E','C','H', 6, 'p','a','#','s',' ','w' };
        uint8_t cmd = 0, plen = 0;
        const uint8_t* pay = nullptr;
        char ssid[33], pass[65];
        check("settings RPC parses", improv::parseRpc(data, sizeof(data), cmd, pay, plen) && cmd == improv::C_WIFI);
        check("SSID and password come out whole, # and space included",
              improv::parseWifi(pay, plen, ssid, sizeof(ssid), pass, sizeof(pass)) &&
              !strcmp(ssid, "MECH") && !strcmp(pass, "pa#s w"));

        const uint8_t open[] = { 3, 'c','a','f', 0 };
        check("an open network has an empty password",
              improv::parseWifi(open, sizeof(open), ssid, sizeof(ssid), pass, sizeof(pass)) &&
              !strcmp(ssid, "caf") && pass[0] == '\0');

        const uint8_t noSsid[] = { 0, 3, 'a','b','c' };
        check("an empty SSID is refused",
              !improv::parseWifi(noSsid, sizeof(noSsid), ssid, sizeof(ssid), pass, sizeof(pass)));

        const uint8_t shortPass[] = { 2, 'a','b', 9, 'x' };
        check("a password length past the payload is refused",
              !improv::parseWifi(shortPass, sizeof(shortPass), ssid, sizeof(ssid), pass, sizeof(pass)));

        const uint8_t lying[] = { 0x01, 40, 1, 'a', 0 };
        check("an RPC length past the packet is refused",
              !improv::parseRpc(lying, sizeof(lying), cmd, pay, plen));

        char tiny[4];
        check("an SSID longer than the buffer is refused, not cut",
              !improv::parseWifi(data + 2, 12, tiny, sizeof(tiny), pass, sizeof(pass)));
    }

    // ---- what the board sends -------------------------------------------
    {
        uint8_t out[64];
        size_t n = improv::stateFrame(out, sizeof(out), improv::S_AUTHORIZED);
        // 0x1DD + 1 + 1 + 1 + 2 = 0x1E2
        const uint8_t want[] = { 'I','M','P','R','O','V', 0x01, 0x01, 0x01, 0x02, 0xE2, '\n' };
        check("state authorized, byte for byte", n == sizeof(want) && !memcmp(out, want, n));

        n = improv::errorFrame(out, sizeof(out), improv::E_CONNECT);
        // 0x1DD + 1 + 2 + 1 + 3 = 0x1E4
        const uint8_t werr[] = { 'I','M','P','R','O','V', 0x01, 0x02, 0x01, 0x03, 0xE4, '\n' };
        check("error unable to connect, byte for byte", n == sizeof(werr) && !memcmp(out, werr, n));

        check("a frame that will not fit is not written", improv::stateFrame(out, 8, 0) == 0);
    }
    {
        uint8_t out[160];
        const char* s[] = { "ab", "c" };
        size_t n = improv::resultFrame(out, sizeof(out), improv::C_INFO, s, 2);
        // data: cmd 3, len 5, 2 'a' 'b', 1 'c'  -> packet length 7
        const uint8_t head[] = { 'I','M','P','R','O','V', 0x01, 0x04, 7, 0x03, 5, 2, 'a','b', 1, 'c' };
        check("a result carries length-prefixed strings", n == sizeof(head) + 2 && !memcmp(out, head, sizeof(head)));

        // The board's own frames must parse, or a board talking to itself
        // over a loopback would be the only thing that could read them.
        Parser p;
        check("and parses back as a result", feedAll(p, out, n) == 1 && p.type() == improv::T_RESULT);

        n = improv::resultFrame(out, sizeof(out), improv::C_SCAN, nullptr, 0);
        const uint8_t end[] = { 'I','M','P','R','O','V', 0x01, 0x04, 2, 0x04, 0 };
        check("the end of a scan is an empty result", n == sizeof(end) + 2 && !memcmp(out, end, sizeof(end)));

        char longS[200];
        memset(longS, 'x', sizeof(longS) - 1);
        longS[sizeof(longS) - 1] = '\0';
        const char* l[] = { longS };
        check("a result too long for a packet is refused",
              improv::resultFrame(out, sizeof(out), improv::C_INFO, l, 1) == 0);
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
