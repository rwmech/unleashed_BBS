/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/config.h
 * Module:       Core / build-time configuration
 *
 * Purpose:      Compile-time configuration for the µnleashed BBS core.
 *                  Every sizing decision for the WROOM-32E reference target
 *                  lives here so plugins and ports can override in one place.
 *                  Run-time settings (timezone, NTP, sysop password, time
 *                  limits) live in data/system.cfg, see sysconfig.h.
 *
 * Design:       One place for every sizing and timing decision, so a port or a plugin can
 *               see the cost of the core at a glance.
 *
 * Notes:        Run-time settings live in data/system.cfg (see sysconfig.h); changing a
 *               value here needs a rebuild.
 *
 * Libraries:    none
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
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

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
// The µ is UTF-8 (C2 B5). Term::text shows it as µ on ANSI and as "u" on
// PETSCII and ASCII. Anything that needs plain ASCII uses BBS_HOSTNAME.
#define BBS_NAME            "\xC2\xB5nleashed BBS"
#define BBS_VERSION         "0.17.4"
#define BBS_HOSTNAME        "unleashed"  // DHCP and mDNS (unleashed.local)

// ---------------------------------------------------------------------------
// Network: one dial-in port, 10 caller nodes, a busy line, a hidden sysop node
// ---------------------------------------------------------------------------
#define BBS_PORT            6400
// Ten caller lines, plus the busy line and the hidden sysop node.
//
// Sixteen did not fit. A Session is 6,000 bytes, so eighteen of them was
// 108,000 bytes of static RAM and the link failed with dram0_0_seg
// overflowed by 104 bytes: not near the limit, past it. The figure that
// matters is not the 320 KB of SRAM the part advertises but what is left
// for statically allocated DRAM once the ROM and the radio have taken
// theirs, and that is a good deal less.
//
// Ten is twelve sessions, 72,000 bytes, which leaves room for the file
// transfer buffers and for whatever comes after them. It is also the sort
// of number the boards this is modelled on actually had.
#define BBS_MAX_NODES       10
#define BBS_LISTEN_BACKLOG  4

// TCP keepalive on every caller socket: a line whose far end vanished
// (C64 switched off, cable pulled) is dropped in about idle + intvl * cnt
#define BBS_KEEPALIVE_IDLE_S   60
#define BBS_KEEPALIVE_INTVL_S  10
#define BBS_KEEPALIVE_CNT      3

// Activity LED (system.cfg activity_led_gpio overrides the pin)
#define BBS_LED_GPIO        2        // blue LED on DOIT-style dev boards, -1 = none
#define BBS_LED_PULSE_MS    40

// How long the line is held open after the exit screen has been sent, so the
// last thing a caller sees is not a screen that flashed past on its way to a
// closed socket. Applies however the call ended.
#define BBS_EXIT_LINGER_MS  5000

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
#define BBS_RX_ROOM         1700     // a key is handled only with this much output room free
                                     // (a full form or user-list redraw is ~1.4 KB)
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
#define BBS_FORM_WARN_MS    120000   // sign-up form before login: warn
#define BBS_FORM_TIMEOUT_MS 180000   // sign-up form before login: hang up
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
// Accounts never move to the SD card: they are the one thing that has to
// survive a card failing, and LittleFS is power-fail safe in a way FAT is
// not. userdata is 608 KB and a UserRec is about 450 bytes, so the space is
// there for roughly 1,380 of them.
//
// The cap is 250 anyway, and it is an index width, not a space limit.
// users::count, users::at, Session::ulSel/ulTop/ulCount and Session::listIdx
// are all uint8_t, and listIdx is the row counter in every list on the board.
// Widening that is mechanical but it touches every list, so it is not a thing
// to do in the same build as a partition move. Raising this later costs no
// erase and no reflash: the space is already allocated.
#define BBS_MAX_USERS       250      // system.cfg max_users; see above
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
// Plugins (PLUGINS.md)
// ---------------------------------------------------------------------------
#define BBS_MAX_PLUGINS     8        // compiled-in plugin table
#define BBS_PLUGIN_TICK_MS  250      // periodic hook cadence
#define BBS_PLUGIN_DIR      "p"      // <fs>/p/<name>/ holds a plugin's files
#define BBS_PLUGIN_QUOTA    65536    // per plugin, onboard
#define BBS_FS_RESERVE      32768    // free space the core keeps for accounts
#define BBS_HEAP_RESERVE    40960    // heap kept free for callers and backups

// ---------------------------------------------------------------------------
// Plugins (PLUGINS.md)
// ---------------------------------------------------------------------------
#define BBS_MAX_PLUGINS     8        // compiled-in plugin table
#define BBS_PLUGIN_TICK_MS  250      // periodic hook cadence
#define BBS_PLUGIN_DIR      "p"      // <fs>/p/<name>/ holds a plugin's files
#define BBS_PLUGIN_QUOTA    65536    // per plugin, onboard
#define BBS_FS_RESERVE      32768    // free space the core keeps for accounts
#define BBS_HEAP_RESERVE    40960    // heap kept free for callers and backups

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
#define BBS_USER_LABEL      "userdata"   // accounts, config, plugin files
#define BBS_USER_BASE       "/userdata"  // never rewritten by uploadfs
#define BBS_FS_LABEL        "storage"
#define BBS_LOGS_MOUNT      "/logs"      // logs partition: fixed-size rings only, never in the zip
#define BBS_LOGS_LABEL      "logs"
#define BBS_SCREEN_DIR      "screens"

// The SD card, when there is one. Optional throughout: a board with no card
// is a complete board, and nothing that has to survive a power cut lives
// here. FAT32 rather than LittleFS on purpose, so the card can be pulled and
// read on any laptop; that is the whole point of it.
#define BBS_SD_MOUNT        "/sd"
// Open handles FATFS keeps room for. Every session has its own
// ScreenPlayer and holds a file open for as long as a screen is playing,
// which at a page break is until the caller presses a key. Five was the
// IDF example's number and it does not survive sixteen nodes: the sixth
// caller to open a screen from the card silently gets the flash copy
// instead, because a failed open here looks exactly like a file that is
// not on the card. Costs about 550 bytes of heap per slot at mount.
#define BBS_SD_MAX_FILES    (BBS_MAX_NODES + 4)
#define BBS_SD_SCREEN_DIR   "screens"    // a card's screens override the stock set
// One line per file, "name description", the way every BBS did it. A plain
// text file inside each area folder so a sysop can edit it on a laptop with
// the card in hand, which is the whole reason the card is FAT32.
#define BBS_FILES_DESC      "FILES.BBS"
// Where the caller log is mirrored on the card, one file per month. The
// ring on the logs partition stays the record LAST reads: it is the sysop's
// security log and must not depend on a card being seated. The card gets a
// plain text copy that can run to months instead of fifty calls.
#define BBS_SD_LOG_DIR      "logs"

#define BBS_CONFIG_FILE     "system.cfg"
#define BBS_CALLLOG_FILE    "calls.log"
#define BBS_REBOOT_FILE     "reboots.log"  // why the board started, one line each
#define BBS_REBOOT_MAX      4096           // bytes before it starts again
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
#define BBS_ZIP_USERS_MAX       163840   // users.txt: 100 full accounts with escaping
#define BBS_ZIP_TOTAL_MAX       360000   // all unpacked files together
#define BBS_SCREEN_NAME_MAX     8        // screen base name, a-z 0-9 _ -
