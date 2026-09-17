/*
 * File:        src/core/sysconfig.h
 * Description: Run-time system configuration from <fs>/system.cfg.
 *              key = value lines, '#' starts a comment. Unknown keys are
 *              logged and ignored; a missing file means defaults.
 *
 *   hostname               DHCP and mDNS name (a-z 0-9 -), applies at boot
 *   tz                     POSIX TZ string, e.g. CST6CDT,M3.2.0,M11.1.0
 *   ntp_server             NTP host name
 *   sysop_password         BYE <pw>: hidden sysop node, every permission
 *   cosysop1_password      BYE <pw>: co-sysop level 1, stays on its node
 *   cosysop2_password      BYE <pw>: co-sysop level 2, stays on its node
 *   idle_minutes           shell idle hangup (warning 1 min before), 0 = never
 *   call_minutes           per-call time limit, 0 = unlimited
 *   day_minutes            per-day time limit, 0 = unlimited
 *   backup_port            HTTP port while the backup window is open
 *   backup_window_minutes  how long a button press keeps the window open
 *   backup_button_gpio     button pin (active low), -1 = no button
 *   self_register          yes: new handles can sign up; no: sysop creates accounts
 *   max_users              account limit (1..100 on onboard storage)
 *   who_refresh_min        WHO n / DASH n lowest refresh, seconds
 *   who_refresh_max        WHO n / DASH n highest refresh, seconds
 *   activity_led_gpio      LED blinked on network traffic, -1 = none
 *
 *   [access] section: one row per permission, columns SYSOP CO1 CO2,
 *   X = allowed, - = denied. The SYSOP column is informational; the
 *   sysop always has everything. Rows left out keep their defaults.
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc stdio)
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
    char     hostname[32]  = BBS_HOSTNAME;
    char     tz[48]        = BBS_DEFAULT_TZ;
    char     ntpServer[64] = BBS_DEFAULT_NTP;
    char     sysopPass[33] = "";
    char     coPass[2][33] = { "", "" };          // [0] level 1, [1] level 2
    uint16_t coPerms[2]    = { static_cast<uint16_t>(PERM_ALL & ~PERM_UNBAN),
                               static_cast<uint16_t>(PERM_NODES | PERM_BROADCAST | PERM_TIME | PERM_BANS |
                                                     PERM_NOLIMITS | PERM_DASH) };
    uint16_t idleMinutes   = BBS_IDLE_MINUTES;
    bool     selfRegister  = true;
    bool     guestEnabled  = true;
    uint16_t guestMinutes  = BBS_GUEST_MINUTES;
    uint8_t  maxUsers      = BBS_MAX_USERS;
    uint8_t  whoMin        = BBS_WHO_REFRESH_MIN;
    uint8_t  whoMax        = BBS_WHO_REFRESH_MAX;
    int8_t   ledGpio       = BBS_LED_GPIO;
    uint16_t callMinutes   = BBS_CALL_MINUTES;
    uint16_t dayMinutes    = BBS_DAY_MINUTES;
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

// redactLine: a password assignment with a value becomes "key = ***".
// Returns true and fills out when the line was rewritten.
bool redactLine(const char* line, char* out, size_t outLen);

// unredactLine: "key = ***" becomes "key = <live password>".
// Returns true and fills out when the line was rewritten.
bool unredactLine(const char* line, char* out, size_t outLen);

} // namespace syscfg
