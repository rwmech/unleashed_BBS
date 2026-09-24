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

// ---------------------------------------------------------------------------
// ClientModel: the installer's reader, ported line for line from the
// vendored ESP Web Tools 10.4.0 (install-dialog-im156JnI.js, _processInput
// and _handleIncomingPacket), so the board's frames are read the way the
// dialog reads them rather than by the board's own parser. Only the parts
// that decide whether a packet is seen and what it does:
//   - a packet is looked for only at the start of a line: a 0x0A anywhere in
//     the first nine bytes starts the line again;
//   - bytes 0..5 must be "IMPROV", the length is 9 + byte 8 + 1;
//   - version must be 1 and the checksum the low byte of the sum;
//   - CURRENT_STATE sets the state and fires "state-changed", whenever it
//     arrives, asked for or not;
//   - RPC_RESULT with no request waiting is logged and dropped.
// ---------------------------------------------------------------------------
struct ClientModel {
    int  state = -1;              // this.state
    int  stateEvents = 0;         // "state-changed" dispatched
    int  droppedResults = 0;      // "Received result while not waiting for one"
    bool waiting = false;         // this._rpcFeedback set
    int  results = 0;

    // e: undefined (-1), false (0: skip to the newline), true (1: in a packet)
    int e = -1;
    uint8_t t[300];
    size_t  n = 0, want = 0;

    void handle() {
        const uint8_t* p = t + 6;
        uint8_t ver = p[0], type = p[1], len = p[2];
        if (ver != 1) return;
        uint8_t sum = 0;
        for (size_t i = 0; i + 1 < n; ++i) sum = static_cast<uint8_t>(sum + t[i]);
        if (sum != p[3 + len]) return;
        if (type == 1) { state = p[3]; ++stateEvents; }
        else if (type == 4) { if (!waiting) ++droppedResults; else ++results; }
    }
    void feed(uint8_t s) {
        if (e == 0) { if (s == 10) e = -1; return; }
        if (e == 1) {
            t[n++] = s;
            if (n == want) { handle(); e = -1; n = 0; }
            return;
        }
        if (s == 10) { n = 0; return; }
        t[n++] = s;
        if (n != 9) return;
        e = !memcmp(t, "IMPROV", 6) ? 1 : 0;
        if (e == 0) { n = 0; return; }
        want = 9u + t[8] + 1u;
    }
    void feedAll(const uint8_t* b, size_t len) { for (size_t i = 0; i < len; ++i) feed(b[i]); }
};

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

    // ---- what the board says unasked (1.1.0) ----------------------------
    {
        check("state: a trial outranks being online",
              improv::stateNow(true, true) == improv::S_PROVISIONING);
        check("state: online is provisioned",
              improv::stateNow(false, true) == improv::S_PROVISIONED);
        check("state: still joining is authorized, not provisioned",
              improv::stateNow(false, false) == improv::S_AUTHORIZED);
    }
    {
        improv::JoinWatch w;
        check("nothing is said to nobody: no client has spoken", !w.joined(true));
        w.heard();
        check("nothing while still joining", !w.joined(false));
        check("once online, it is said", w.joined(true));
        check("and only once", !w.joined(true));
        check("not after a drop and a reconnect either", !w.joined(false) && !w.joined(true));
    }
    {
        // Improv tried a new network before the board had joined its own.
        // The trial's answer says it; a failed trial rejoins the old network,
        // and that join must not be announced as though the new one worked.
        improv::JoinWatch w;
        w.heard();
        w.trialStarted();
        check("a trial silences it", !w.joined(true));
    }
    {
        // A port open that does not reset the board: it is already online
        // when asked, and the answer said so, with the link. Nothing to add.
        improv::JoinWatch w;
        w.heard();
        w.answered(improv::S_AUTHORIZED);
        check("an answer of 'still joining' leaves it to be said", w.joined(true));
        improv::JoinWatch v;
        v.heard();
        v.answered(improv::S_PROVISIONED);
        check("an answer that already said provisioned is not repeated", !v.joined(true));
    }
    {
        // The frame it sends, byte for byte: 0x1DD + 1 + 1 + 1 + 4 = 0x1E4.
        uint8_t out[16];
        size_t n = improv::stateFrame(out, sizeof(out), improv::S_PROVISIONED);
        const uint8_t want[] = { 'I','M','P','R','O','V', 0x01, 0x01, 0x01, 0x04, 0xE4, '\n' };
        check("state provisioned, byte for byte", n == sizeof(want) && !memcmp(out, want, n));

        // As the dialog reads it: after a log line cut off mid-way, which is
        // why the board sends a newline first (imp::send).
        ClientModel c;
        const char log[] = "I (4210) main: online 192.168.0.40  dial";
        c.feedAll(reinterpret_cast<const uint8_t*>(log), strlen(log));
        const uint8_t nl = '\n';
        c.feedAll(&nl, 1);
        c.feedAll(out, n);
        check("the installer's reader takes it unasked, and redraws",
              c.state == improv::S_PROVISIONED && c.stateEvents == 1);

        // Without the newline in front, the reader never sees it: the
        // reason every frame goes out after one.
        ClientModel d;
        d.feedAll(reinterpret_cast<const uint8_t*>(log), strlen(log));
        d.feedAll(out, n);
        check("but not glued to the end of a log line", d.stateEvents == 0);

        // Why the URL is not sent the same way: the reader drops a result
        // nobody asked for, so it would only put an error in the console.
        uint8_t r[80];
        const char* s[] = { "telnet://192.168.0.40:6400" };
        size_t rn = improv::resultFrame(r, sizeof(r), improv::C_STATE, s, 1);
        ClientModel e;
        e.feedAll(&nl, 1);
        e.feedAll(r, rn);
        check("an unasked result is dropped by the installer", e.droppedResults == 1 && e.results == 0);
    }

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
