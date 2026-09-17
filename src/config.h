/*
 * File:        src/config.h
 * Description: Compile-time configuration for the µnleashed BBS core.
 *              Every sizing decision for the WROOM-32E reference target
 *              lives here so plugins and ports can override in one place.
 *              Run-time settings (timezone, NTP, sysop password, time
 *              limits) live in data/system.cfg, see sysconfig.h.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
// The µ is UTF-8 (C2 B5). Term::text shows it as µ on ANSI and as "u" on
// PETSCII and ASCII. Anything that needs plain ASCII uses BBS_HOSTNAME.
#define BBS_NAME            "\xC2\xB5nleashed BBS"
#define BBS_VERSION         "0.7.0"
#define BBS_HOSTNAME        "unleashed"  // DHCP and mDNS (unleashed.local)

// ---------------------------------------------------------------------------
// Network: one dial-in port, 6 caller nodes, a busy line, a hidden sysop node
// ---------------------------------------------------------------------------
#define BBS_PORT            6400
#define BBS_MAX_NODES       6        // caller nodes (DDial-style 6 lines)
#define BBS_LISTEN_BACKLOG  4

// TCP keepalive on every caller socket: a line whose far end vanished
// (C64 switched off, cable pulled) is dropped in about idle + intvl * cnt
#define BBS_KEEPALIVE_IDLE_S   60
#define BBS_KEEPALIVE_INTVL_S  10
#define BBS_KEEPALIVE_CNT      3

// Activity LED (system.cfg activity_led_gpio overrides the pin)
#define BBS_LED_GPIO        2        // blue LED on DOIT-style dev boards, -1 = none
#define BBS_LED_PULSE_MS    40

// ---------------------------------------------------------------------------
// Terminal detection
// ---------------------------------------------------------------------------
#define BBS_SETTLE_MS           300  // wait for a telnet client's IAC before probing
#define BBS_DETECT_TIMEOUT_MS   2000 // wait for ANSI cursor-position reply
#define BBS_DETECT_CPR2_MS      500  // extra wait for 2nd reply (UTF-8 test)
#define BBS_DETECT_DOT_MS       250  // "DETECTING TERMINAL...." dot cadence
#define BBS_KEY_PROMPT_MS       60000
#define BBS_KEY_PROMPT_TRIES    3

// ---------------------------------------------------------------------------
// Session sizing (fixed, preallocated at boot, no heap after boot)
// ---------------------------------------------------------------------------
#define BBS_TL_BYTES        3072     // per-session timed output buffer (a PETSCII form redraw is ~1.2 KB)
#define BBS_RX_ROOM         1024     // a key is handled only with this much output room free
#define BBS_TL_FRAMES       96       // per-session timed output frames
#define BBS_RX_CHUNK        64       // bytes read per select pass
#define BBS_LINE_MAX        72       // line editor capacity
#define BBS_USER_MAX        20       // handle length
#define BBS_SCREEN_CHUNK    96       // screen file read chunk
#define BBS_HISTORY         4        // command lines kept for up-arrow recall
#define BBS_MASK_SHOW       24       // most '*' a masked input shows (keeps it on one line)
#define BBS_DOING_MAX       10       // staff WHO/DASH: last command verb shown
#define BBS_BUS_DEPTH       4        // queued messages per session

// ---------------------------------------------------------------------------
// Timeouts (all idle timeouts warn first)
// ---------------------------------------------------------------------------
#define BBS_NAME_WARN_MS    30000    // handle prompt: warn
#define BBS_NAME_TIMEOUT_MS 60000    // handle prompt: hang up
#define BBS_IDLE_WARN_BEFORE_MS 60000 // shell: warn this long before the idle hangup
#define BBS_TIME_WARN1_S    300      // time limit warnings (seconds left)
#define BBS_TIME_WARN2_S    60

// ---------------------------------------------------------------------------
// Busy line
// ---------------------------------------------------------------------------
#define BBS_BUSY_COUNTDOWN  10       // seconds before the busy line hangs up
#define BBS_BUSY_LOGIN_MS   60000    // a busy-line login must elevate to sysop in this

// ---------------------------------------------------------------------------
// Defaults for system.cfg keys
// ---------------------------------------------------------------------------
#define BBS_IDLE_MINUTES    20       // shell idle hangup, 0 = never
#define BBS_WHO_REFRESH_MIN 1        // WHO n / DASH n refresh bounds, seconds
#define BBS_WHO_REFRESH_MAX 30
#define BBS_CALL_MINUTES    60       // per call, 0 = unlimited
#define BBS_DAY_MINUTES     480      // per day, 0 = unlimited
#define BBS_GUEST_MINUTES   15       // per guest call, 0 = unlimited; guests have no daily limit
#define BBS_DEFAULT_TZ      "UTC0"
#define BBS_DEFAULT_NTP     "pool.ntp.org"

// ---------------------------------------------------------------------------
// Security
// ---------------------------------------------------------------------------
#define BBS_BAN_SLOTS       8
#define BBS_BAN_TRIES       3        // wrong sysop passwords per window
#define BBS_BAN_WINDOW_MS   900000   // 15 minutes
#define BBS_BAN_MS          900000   // 15 minutes

// ---------------------------------------------------------------------------
// User accounts (users.txt on the storage partition, in the backup zip)
// ---------------------------------------------------------------------------
#define BBS_USERS_FILE      "users.txt"
#define BBS_MAX_USERS       100      // system.cfg max_users; more needs the SD card plugin
#define BBS_PROFILE_MAX     148      // 4 rows of 37 columns on a C64
#define BBS_PASS_MIN        4
#define BBS_PASS_MAX        32
#define BBS_PASS_ROUNDS     1000     // salted SHA-256 repeated
#define BBS_LOGIN_TRIES     3        // wrong passwords per call, then hang up
#define BBS_LOCK_SLOTS      8        // handles tracked for lockout (RAM)
#define BBS_LOCK_FAILS      5        // wrong passwords per handle in the window
#define BBS_LOCK_WINDOW_MS  900000   // 15 minutes
#define BBS_LOCK_MS         900000   // 15 minutes locked
#define BBS_USERLIST_ROWS   24       // user manager rows buffered at once

// ---------------------------------------------------------------------------
// Scheduler / task
// ---------------------------------------------------------------------------
#define BBS_SELECT_MS       10       // loop cadence, also effect resolution
#define BBS_TASK_CORE       1        // Wi-Fi lives on core 0
#define BBS_TASK_STACK      8192
#define BBS_TASK_PRIO       5

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------
#define BBS_FS_MOUNT        "/fs"        // storage partition: system.cfg, screens (the backup zip)
#define BBS_FS_LABEL        "storage"
#define BBS_LOGS_MOUNT      "/logs"      // logs partition: fixed-size rings only, never in the zip
#define BBS_LOGS_LABEL      "logs"
#define BBS_SCREEN_DIR      "screens"
#define BBS_CONFIG_FILE     "system.cfg"
#define BBS_CALLLOG_FILE    "calls.log"
#define BBS_CALLLOG_SIZE    50

// ---------------------------------------------------------------------------
// Backup window (HTTP, only while open). Defaults for system.cfg keys.
// ---------------------------------------------------------------------------
#define BBS_BACKUP_PORT         8080
#define BBS_BACKUP_MINUTES      5
#define BBS_BACKUP_GPIO         0        // BOOT button on ESP32 dev boards
#define BBS_BACKUP_APPROVE_MS   120000   // sysop has this long to answer Y/N
#define BBS_BACKUP_IDLE_MS      30000    // HTTP client silent this long: dropped
#define BBS_BACKUP_HEADER_MS    10000    // whole request header must arrive in this
#define BBS_BACKUP_TRANSFER_MS  180000   // whole upload body or download in this
#define BBS_BACKUP_STAGING      ".staging"

// Upload limits (enforced, documented in SCREENS.md)
#define BBS_ZIP_MAX_BYTES       400000   // the uploaded .zip itself
#define BBS_ZIP_MAX_FILES       64       // accepted files per upload
#define BBS_ZIP_FILE_MAX        65536    // one unpacked file
#define BBS_ZIP_TOTAL_MAX       360000   // all unpacked files together
#define BBS_SCREEN_NAME_MAX     8        // screen base name, a-z 0-9 _ -
