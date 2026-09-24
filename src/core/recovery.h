/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/recovery.h
 * Module:       Core / recovery without a reflash (1.1.0)
 *
 * Purpose:      Two ways a board puts itself right with nobody at a keyboard.
 *
 *                  The BOOT-hold reset. BOOT pressed within the first
 *                  kWindowMs of the firmware running and held: what happens
 *                  depends on how long, and happens on release. Under 7 s
 *                  nothing; 7 to 15 s the sysop password goes back to the
 *                  published default; 15 to 20 s a factory reset of userdata
 *                  and logs; held to 20 s the whole thing is abandoned. The
 *                  activity LED shows the stage while it is held.
 *
 *                  The last network that worked. Kept in userdata/wifi.last
 *                  and never in system.cfg: it is a fact about this board's
 *                  radio, not a setting, so it has no business on a CONFIG
 *                  page or in a backup restored onto another board. A board
 *                  dialling a different network that has not joined within
 *                  kWifiFallbackMs of boot goes back to it.
 *
 *                  The two state machines are pure and header-only, so
 *                  host/test_recovery.cpp can drive them on a simulated clock
 *                  at every boundary. The rest reads the button, the files
 *                  and the LED through the platform layer.
 *
 * Interfaces:   BootHold, WifiFallback, bootPoll, bootWatching, bootLed,
 *               noteText, lastGood, wifiBegin, wifiJoined, wifiDue
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md, "Resetting the board"
 *
 * Copyright 2026 - Robert Mech
 * License:      GNU General Public License v2 or later
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */

#pragma once
#include <cstdint>
#include <cstddef>

namespace recovery {

// ===========================================================================
// Restart notes: why the board restarted itself. The platform carries one
// across the restart (plat::restart, plat::restartNote), and Bbs::noteBoot
// writes it to reboots.log in place of "software restart", which is what the
// chip alone would say, so the next staff login is told what really happened.
// ===========================================================================
// NOTE_FACTORY_FAILED: the factory erase did not finish (1.1.0). Without it
// the restart that follows read as a plain "software restart" and nothing
// was said at login, and of everything a BOOT reset can end in, a half-
// erased board is the one that most needs saying. Appended: a note is a
// number carried across the restart, so the others keep theirs.
enum Note : uint8_t { NOTE_NONE = 0, NOTE_PASSWORD = 1, NOTE_FACTORY = 2, NOTE_FACTORY_FAILED = 3 };

// noteText: the words for reboots.log, SYS and the staff login, or nullptr
// for no note. Short enough for "Last restart: <text>." in 39 columns, and
// for the 32 byte boot reason they are kept in. None of them may contain
// "crash", "watchdog" or "brownout": reboots.log is counted by those words
// (Bbs::noteBoot), and a reset somebody did on purpose is not a crash.
inline const char* noteText(uint8_t note) {
    switch (note) {
        case NOTE_PASSWORD:       return "password reset by BOOT";
        case NOTE_FACTORY:        return "factory reset by BOOT";
        case NOTE_FACTORY_FAILED: return "factory reset FAILED";
        default:                  return nullptr;
    }
}

// ===========================================================================
// BOOT-hold reset
// ===========================================================================

// The press must START within this long of the firmware starting. A second
// or so to move a finger from RESET to BOOT, with room to spare; after it,
// GPIO0 is the backup window's button again. A hold that has started keeps
// being watched until it is let go, however long that is.
constexpr uint32_t kWindowMs   = 10000;
// Up for this long is a release rather than a bounce. Contacts bounce for a
// few milliseconds at each edge, and a bounce read as a release part way
// through a 16 s hold would act on the wrong band.
constexpr uint32_t kReleaseMs  = 100;
constexpr uint32_t kPasswordMs = 7000;
constexpr uint32_t kFactoryMs  = 15000;
constexpr uint32_t kAbortMs    = 20000;

// Stage: where a hold is. The order matters: a hold only ever moves forward,
// one stage at a time, and what happens on release is the stage it had
// reached, which is to say what the LED and the console said when the
// button was let go.
enum class Stage : uint8_t { Waiting, Counting, Password, Factory, Abort, Done };

// Led: what the activity LED is doing for the watch. Free means the watch
// has let go of it and traffic drives it as usual.
enum class Led : uint8_t { Free, Slow, Fast, Solid, Off };

// BootHold: the timing, and nothing else. Time and the button in, events out.
class BootHold {
public:
    enum class Ev : uint8_t { None, Held, Stage, Released, Closed };
    struct Event {
        Ev       ev;
        Stage    stage;     // Stage: the one just reached; Released: the one it ended in
        // Stage: how long so far. Released: how long it was last SEEN held,
        // not when it went up, so the number said on release always agrees
        // with the stage acted on: a board that last saw 6.98 s says 6, and
        // does nothing, even if the finger came off at 7.01 s.
        uint32_t heldMs;
    };

    // feed: one look at the button. down is true while BOOT is pressed.
    Event feed(uint32_t now, bool down) {
        Event e{ Ev::None, stage_, 0 };
        if (stage_ == Stage::Done) return e;
        if (stage_ == Stage::Waiting) {
            if (now >= kWindowMs) {                 // the window closed with nobody on it
                stage_ = Stage::Done;
                e.ev = Ev::Closed;
                e.stage = stage_;
            } else if (down) {
                stage_   = Stage::Counting;
                pressAt_ = downAt_ = now;
                up_      = false;
                e.ev = Ev::Held;
                e.stage = stage_;
            }
            return e;
        }
        if (down) {
            up_ = false;                            // a bounce, not a release
            downAt_ = now;
            uint32_t held = now - pressAt_;
            Stage want = held >= kAbortMs    ? Stage::Abort
                       : held >= kFactoryMs  ? Stage::Factory
                       : held >= kPasswordMs ? Stage::Password
                                             : Stage::Counting;
            // One stage a feed, so a pass that ran long still says each one.
            if (static_cast<uint8_t>(want) > static_cast<uint8_t>(stage_)) {
                stage_ = static_cast<Stage>(static_cast<uint8_t>(stage_) + 1);
                e.ev = Ev::Stage;
                e.stage = stage_;
                e.heldMs = held;
            }
            return e;
        }
        if (!up_) { up_ = true; upAt_ = now; }
        if (now - upAt_ < kReleaseMs) return e;     // not yet a release
        e.ev = Ev::Released;
        e.stage = stage_;
        e.heldMs = downAt_ - pressAt_;
        stage_ = Stage::Done;
        return e;
    }

    Stage stage() const { return stage_; }
    bool  watching() const { return stage_ != Stage::Done; }

    // led: the pattern for the stage. Slow blink while counting, rapid
    // flashing from 7 s, solid from 15 s, off from 20 s.
    Led led() const {
        switch (stage_) {
            case Stage::Counting: return Led::Slow;
            case Stage::Password: return Led::Fast;
            case Stage::Factory:  return Led::Solid;
            case Stage::Abort:    return Led::Off;
            default:              return Led::Free;
        }
    }

    // ledOn: whether the LED is lit at this instant. Timed from the press,
    // so the first blink comes on the moment the hold is seen.
    bool ledOn(uint32_t now) const {
        uint32_t t = now - pressAt_;
        switch (led()) {
            case Led::Slow:  return t % 1000u < 500u;     // 1 Hz
            case Led::Fast:  return t % 200u < 100u;      // 5 Hz
            case Led::Solid: return true;
            default:         return false;
        }
    }

private:
    Stage    stage_   = Stage::Waiting;
    bool     up_      = false;     // seen up since the last down, not yet a release
    uint32_t pressAt_ = 0;
    uint32_t downAt_  = 0;         // the last look that saw it held
    uint32_t upAt_    = 0;
};

// bootPoll: the board's watch. Reads the button through the platform, runs
// the LED, says each stage on the console and acts on the release. Cheap to
// call every pass: once the watch is over it is one comparison. Returns
// whether it is still watching.
bool bootPoll(uint32_t now);

// bootWatching: the watch still owns GPIO0. The backup window's button is
// the same pin and leaves it alone until this goes false.
bool bootWatching();

// bootLed: the pattern the watch is showing, for the host build's trace.
Led bootLed();

// ===========================================================================
// Wi-Fi: the last network that worked
// ===========================================================================

constexpr uint32_t kWifiFallbackMs = 60000;

// WifiFallback: when to give up on a network and go back. Armed at boot only
// when there is a last good network and it is not the one being dialled; any
// join disarms it; it waits while Improv has the radio.
class WifiFallback {
public:
    void begin(uint32_t now, bool armed) { armed_ = armed; at_ = now; }

    // due: true once, when the time is up and nothing has joined.
    bool due(uint32_t now, bool joined, bool busy) {
        if (!armed_) return false;
        if (joined) { armed_ = false; return false; }
        if (busy || now - at_ < kWifiFallbackMs) return false;
        armed_ = false;
        return true;
    }

    bool armed() const { return armed_; }

private:
    bool     armed_ = false;
    uint32_t at_    = 0;
};

// lastGood: the last network this board joined, from userdata. False when
// there is none, which is every board until its first join on 1.1.0.
bool lastGood(char (&ssid)[33], char (&pass)[65]);

// wifiBegin: at boot, once the network this boot dials is known. Arms the
// fallback when it is not the last one that worked, and says so.
void wifiBegin(uint32_t now, const char* ssid, const char* pass);

// wifiJoined: the board is on ssid. Kept as the last good network if it is
// not already, so the file is written when the network changes, not at every
// join.
void wifiJoined(const char* ssid, const char* pass);

// wifiDue: true once, when the network being dialled (from) has not joined in
// time: the last good one is in ssid and pass, and the console has been told.
// The caller moves the radio.
bool wifiDue(uint32_t now, bool up, bool busy, const char* from,
             char (&ssid)[33], char (&pass)[65]);

} // namespace recovery
