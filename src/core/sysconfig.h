/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/sysconfig.h
 * Module:       Core / run-time configuration
 *
 * Purpose:      Run-time system configuration from <fs>/system.cfg.
 *                  key = value lines, '#' starts a comment. Unknown keys are
 *                  logged and ignored; a missing file means defaults.
 *
 *                  hostname               DHCP and mDNS name (a-z 0-9 -), applies at boot
 *                  tz                     POSIX TZ string, e.g. CST6CDT,M3.2.0,M11.1.0
 *                  ntp_server             NTP host name
 *                  sysop_password         BYE <pw>: hidden sysop node, every permission
 *                  cosysop1_password      BYE <pw>: co-sysop level 1, stays on its node
 *                  cosysop2_password      BYE <pw>: co-sysop level 2, stays on its node
 *                  idle_minutes           shell idle hangup (warning 1 min before), 0 = never
 *                  call_minutes           per-call time limit, 0 = unlimited
 *                  day_minutes            per-day time limit, 0 = unlimited
 *                  backup_port            HTTP port while the backup window is open
 *                  backup_window_minutes  how long a button press keeps the window open
 *                  backup_button_gpio     button pin (active low), -1 = no button
 *                  self_register          yes: new handles can sign up; no: sysop creates accounts
 *                  max_users              account limit (1..250)
 *                  who_refresh_min        WHO n / DASH n lowest refresh, seconds
 *                  who_refresh_max        WHO n / DASH n highest refresh, seconds
 *                  activity_led_gpio      LED blinked on network traffic, -1 = none
 *                  wifi_ssid              the network to join, applies at boot
 *                  wifi_password          its passphrase (kept in a backup, which
 *                                         only a local address can download)
 *
 *                  The password keys and wifi_ssid take the rest of the line
 *                  as it stands: a '#' there is part of the value, because a
 *                  passphrase may contain one and Improv writes whatever the
 *                  browser was given.
 *
 *                  [access] section: one row per permission, columns SYSOP CO1 CO2,
 *                  X = allowed, - = denied. The SYSOP column is informational; the
 *                  sysop always has everything. Rows left out keep their defaults.
 *
 * Libraries:    none (libc stdio)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     COMMANDS.md
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
#include "../config.h"

// Staff levels, ordered: a higher level outranks a lower one
enum class Access : uint8_t { None, CoSysop2, CoSysop1, Sysop };

// Permission bits for the [access] matrix
enum Perm : uint16_t {
    PERM_NODES     = 1 << 0,   // NODES list with IPs, IPs in LAST
    PERM_KICK      = 1 << 1,   // KICK
    PERM_BROADCAST = 1 << 2,   // BROADCAST
    PERM_SNOOP     = 1 << 3,   // SNOOP
    PERM_TIME      = 1 << 4,   // TIME n +/-m
    PERM_BANS      = 1 << 5,   // BANS
    PERM_UNBAN     = 1 << 6,   // UNBAN
    PERM_HIDE      = 1 << 7,   // SHOW / HIDE / LURK
    PERM_NOLIMITS  = 1 << 8,   // no idle timeout, no call or daily limit
    PERM_DASH      = 1 << 9,   // DASH dashboard
    PERM_USERS     = 1 << 10,  // USERS manager, USER ADD/EDIT/DEL, private fields in INFO
    PERM_ALL       = (1 << 11) - 1,
};

struct PermName { const char* name; uint16_t bit; };
extern const PermName kPermNames[];
extern const uint8_t  kPermCount;

struct SysConfig {
    // What this board calls itself, as opposed to what the software is
    // called. Empty falls back to the software name, so a board that has
    // never been told still introduces itself as something.
    char     boardName[41] = "";
    char     hostname[32]  = BBS_HOSTNAME;
    char     tz[48]        = BBS_DEFAULT_TZ;
    char     ntpServer[64] = BBS_DEFAULT_NTP;
    char     sysopPass[33] = "";
    char     coPass[2][33] = { "", "" };          // [0] level 1, [1] level 2
    // Wi-Fi, set by Improv over USB or by hand. Empty falls back to
    // include/secrets.h when a build has one, so a developer's board keeps
    // working and a published binary carries nobody's network.
    char     wifiSsid[33]  = "";
    char     wifiPass[65]  = "";
    // True while the sysop password is the published default, which is the
    // case exactly when system.cfg carries no sysop_password line. The board
    // honours it only from its own network, offers setup to local callers,
    // and keeps itself out of the directory until it goes false.
    bool     sysopDefault  = false;
    uint16_t coPerms[2]    = { static_cast<uint16_t>(PERM_ALL & ~PERM_UNBAN),
                               static_cast<uint16_t>(PERM_NODES | PERM_BROADCAST | PERM_TIME | PERM_BANS |
                                                     PERM_NOLIMITS | PERM_DASH) };
    uint16_t idleMinutes   = BBS_IDLE_MINUTES;
    // Where a caller lands when their account does not say. Numbered as the
    // LAND_* values in users.h; not including that header here keeps the
    // config struct free of an account dependency it does not otherwise
    // have, and the one place they must agree is the parser below.
    uint8_t  landing       = 1;            // LAND_MAIN
    bool     selfRegister  = true;
    bool     guestEnabled  = true;
    uint16_t guestMinutes  = BBS_GUEST_MINUTES;
    uint8_t  maxUsers      = BBS_MAX_USERS;
    uint8_t  whoMin        = BBS_WHO_REFRESH_MIN;
    uint8_t  whoMax        = BBS_WHO_REFRESH_MAX;
    int8_t   ledGpio       = BBS_LED_GPIO;
    uint16_t callMinutes   = BBS_CALL_MINUTES;
    uint16_t dayMinutes    = BBS_DAY_MINUTES;
    // The port callers dial (1.1.0). Read at boot and bound once: a change
    // is used from the next restart, so what the listener holds right now
    // is Bbs::port(), and that is what anything telling a caller should say.
    uint16_t port          = BBS_PORT;
    uint16_t backupPort    = BBS_BACKUP_PORT;
    uint16_t backupMinutes = BBS_BACKUP_MINUTES;
    int8_t   backupGpio    = BBS_BACKUP_GPIO;
    bool     fromFile      = false;
};

namespace syscfg {

// load: read <fs>/system.cfg once at boot into the live config, apply TZ
bool load();

// reload: parse the file again. On problems the live config is kept and
// false is returned with the first problem in err.
bool reload(char* err, size_t errLen);

// parseFile: parse any config file into out. Returns the number of hard
// problems (bad value, bad [access] row); the first is copied to err.
int parseFile(const char* path, SysConfig& out, char* err, size_t errLen);

// check: parse a config file that is not the live one (a restore's uploaded
// system.cfg) into the scratch that reload and trial use, and forget it
// again. Returns the number of hard problems, the first in err, exactly as
// parseFile does. maxUsers, when not null, gets the file's max_users, which
// is the one value a restore reads out of the file it is validating.
//
// It exists so the backup restore does not keep two SysConfig copies of its
// own: every byte added to SysConfig used to cost four, one of them each for
// two probes that were only ever used for the length of one call.
int check(const char* path, char* err, size_t errLen, uint8_t* maxUsers = nullptr);

// get: the active configuration
const SysConfig& get();

// passwordLevel: which staff password matches (constant time, every
// password compared). Access::None when nothing matches or none is set.
Access passwordLevel(const char* candidate);

// permsFor: permission bits for a staff level
uint16_t permsFor(Access level);

// anyPassword: true if at least one staff password is configured
bool anyPassword();

// levelName: "Sysop", "Co-sysop 1", "Co-sysop 2", ""
const char* levelName(Access level);

// ---------------------------------------------------------------------------
// write: change these keys in system.cfg and leave everything else exactly
// as it was, comments and ordering included. section is the [name] the keys
// live under, or null for the top of the file; a section that is not there
// yet is added at the end. The file is written through a temp file and a
// rename, so an interrupted write cannot lose the settings.
// ---------------------------------------------------------------------------
struct KeyVal { const char* key; const char* value; };
bool write(const KeyVal* pairs, uint8_t count, const char* section, char* err, size_t errLen);

// ---------------------------------------------------------------------------
// The parser's own rules, for anything that writes the file.
//
// CONFIG used to keep a second copy of them beside its field table, and the
// copies drifted: it wrote "hostname = therustyantenna.local" and the reload
// refused the whole file, it offered 1..120 for a key the parser took as
// 1..60, and it wrote a backup port the parser then rejected. Nothing on the
// page went live and the sysop was told "saved". A writer asks these now,
// so there is one rule and it cannot be copied wrong.
// ---------------------------------------------------------------------------

// validHostname: a-z 0-9 -, 1..31, not starting or ending with '-'. Lower
// cases v in place, as the parser does before it stores the name.
bool validHostname(char* v);

// normaliseHostname: what a person means by a hostname, as the parser wants
// it. Trims, lower cases, and drops one trailing ".local" and any trailing
// dot, because the name a sysop sees for their board is "name.local" and
// that is what they will type. Does not validate: validHostname does that.
void normaliseHostname(char* v);

// pinProblem: why a GPIO number is not one this board may be given, or
// nullptr when it is. GPIO 6 to 11 are wired to the flash chip the firmware
// runs from on a WROOM: driving one does not produce an error, it stops the
// board. The one rule for every pin setting, core and plugin alike, so an
// LED, a button, an SD card and a serial port cannot disagree about it.
// Whether a pin may be input only is the caller's business, not this rule's.
const char* pinProblem(long pin);

// trial: would the parser accept the live configuration with these pairs
// applied on top? Every rule a line meets on its way in is applied: the '#'
// that starts a comment, the "***" that stands for a redacted password, the
// trimming, each key's own check, and the rules about two keys at once.
// Returns nullptr when it would, or the key it objects to, with the parser's
// own message (short, and without the line number) in why.
const char* trial(const KeyVal* pairs, uint8_t count, char* why, size_t n);

// redactLine: a password assignment with a value becomes "key = ***".
// Returns true and fills out when the line was rewritten.
bool redactLine(const char* line, char* out, size_t outLen);

// unredactLine: a staff password line from a restored system.cfg.
// "key = ***" becomes "key = <live password>". A line whose value would be
// the published default, from *** or typed out, comes back as "" and is to
// be left out, so a restore never turns the default into a real password.
// Returns true and fills out when the line was rewritten or is to be dropped.
bool unredactLine(const char* line, char* out, size_t outLen);

} // namespace syscfg
