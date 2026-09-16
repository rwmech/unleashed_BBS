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
#define BBS_VERSION         "0.3.0"
#define BBS_HOSTNAME        "unleashed"  // DHCP and mDNS (unleashed.local)

// ---------------------------------------------------------------------------
// Network: one dial-in port, 6 caller nodes, a busy line, a hidden sysop node
// ---------------------------------------------------------------------------
#define BBS_PORT            6400
#define BBS_MAX_NODES       6        // caller nodes (DDial-style 6 lines)
#define BBS_LISTEN_BACKLOG  4

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
#define BBS_TL_BYTES        2048     // per-session timed output buffer
#define BBS_TL_FRAMES       96       // per-session timed output frames
#define BBS_RX_CHUNK        64       // bytes read per select pass
#define BBS_LINE_MAX        72       // line editor capacity
#define BBS_USER_MAX        20       // handle length
#define BBS_SCREEN_CHUNK    96       // screen file read chunk
#define BBS_HISTORY         4        // command lines kept for up-arrow recall
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
#define BBS_CALL_MINUTES    60       // per call, 0 = unlimited
#define BBS_DAY_MINUTES     480      // per day, 0 = unlimited
#define BBS_DEFAULT_TZ      "UTC0"
#define BBS_DEFAULT_NTP     "pool.ntp.org"

// ---------------------------------------------------------------------------
// Security
// ---------------------------------------------------------------------------
#define BBS_BAN_SLOTS       8
#define BBS_BAN_TRIES       3        // wrong sysop passwords per window
#define BBS_BAN_WINDOW_MS   900000   // 15 minutes
#define BBS_BAN_MS          900000   // 15 minutes
#define BBS_TIMEBANK_SLOTS  16       // daily minutes, keyed by handle + IP until C2

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
#define BBS_FS_MOUNT        "/fs"
#define BBS_FS_LABEL        "storage"
#define BBS_SCREEN_DIR      "screens"
#define BBS_CONFIG_FILE     "system.cfg"
#define BBS_CALLLOG_FILE    "calls.log"
#define BBS_CALLLOG_SIZE    50
