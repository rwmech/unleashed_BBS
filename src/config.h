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
 * License:      GNU General Public License v3 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>. The full
 * text is in the LICENSE file at the top of this repository.
 * ===========================================================================
 */

#pragma once

// What differs between boards: the pins a profile ships with and the
// capabilities it adds. The reference board's values are its defaults.
#include "board.h"

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
// The µ is UTF-8 (C2 B5). Term::text shows it as µ on ANSI and as "u" on
// PETSCII and ASCII. Anything that needs plain ASCII uses BBS_HOSTNAME.
#define BBS_NAME            "\xC2\xB5nleashed BBS"
#define BBS_VERSION         "1.1.0-dev.11"
#define BBS_HOSTNAME        "unleashed"  // DHCP and mDNS (unleashed.local)

// BBS_VERSION_SHOWN: the version as every place a person reads one shows it
// (1.1.0): the welcome screen's @VER@, ABOUT, SYS, MEM, DASH, the boot line
// and Improv's device info. The core version alone on the reference board,
// and the core version with the board profile's own after it on a board
// that has one (board.h): "1.1.0 (S3 1.0.0)". Plain ASCII on purpose: the
// terminal layer turns only the micro sign from UTF-8 into a glyph, so a
// middle dot would reach a C64 or a plain ASCII terminal as "??". The one
// definition, so the places cannot drift. The directory is still sent the
// core version alone (announce), which is what it compares for its update
// arrow; tools/release.py reads this format out of board.h for version.txt.
#ifdef BBS_BOARD_VERSION
#define BBS_VERSION_SHOWN   BBS_VERSION " (" BBS_BOARD_TAG " " BBS_BOARD_VERSION ")"
#else
#define BBS_VERSION_SHOWN   BBS_VERSION
#endif

// ---------------------------------------------------------------------------
// Network: one dial-in port, 10 caller nodes, a busy line, a hidden sysop node
// ---------------------------------------------------------------------------
// The dial-in port a board ships with. `port` in system.cfg moves it (1.1.0),
// from the next restart; Bbs::port() is the one the listener actually holds.
#define BBS_PORT            6400
// Ten caller lines, plus the busy line and the hidden sysop node.
//
// Sixteen did not fit. A Session was 6,000 bytes when this was written and
// is 6,980 now (measured 2026-09-22; the unified composer put 1,153 of
// that on every session). Eighteen of them was
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

// Activity LED (system.cfg activity_led_gpio overrides the pin). The pin a
// board ships with is BBS_LED_GPIO in board.h: 2 on the WROOM, none on a
// board whose only lamp is a WS2812B.
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

// A message being written: a forum post, a mail message, and whatever wants
// the same editor next. ONE buffer per session, shared by every subsystem,
// because a caller can only be writing one thing at a time and two buffers
// per caller was always one too many.
//
// This was learned the expensive way: forums and mail each kept their own,
// 12 x 1729 plus 12 x 513, and the link failed with dram0_0_seg overflowed
// by 8,200 bytes. 16 lines of 72 is a long message for a board whose house
// style is a screenful, and mail's record only ever held 512 characters.
#define BBS_COMPOSE_MAX     1536     // 16 lines x 72
#define BBS_COMPOSE_ROWS    32
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
// The last account to elevate to sysop (1.1.0), on userdata: where missed
// rings go while CONFIG names no sysop account. An id into users.txt, so a
// restore that replaces users.txt removes it (ziparc).
#define BBS_SYSOP_LAST_FILE "sysop.last"
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
// The sysop password a board has before anybody sets one (1.0.0, Rob). It
// is published on the install page, so it is honoured only from the board's
// own network and only while system.cfg has no sysop_password line at all:
// a present-but-empty line still means "no sysop". Nothing on the board will
// store this value as a chosen password: CONFIG refuses it, and a backup
// restore leaves out any staff line that would carry it (1.0.2).
#define BBS_DEFAULT_SYSOP   "unleashed"
#define BBS_LOCK_SLOTS      8        // handles tracked for lockout (RAM)
#define BBS_LOCK_FAILS      5        // wrong passwords per handle in the window
#define BBS_LOCK_WINDOW_MS  900000   // 15 minutes
#define BBS_LOCK_MS         900000   // 15 minutes locked
#define BBS_USERLIST_ROWS   24       // user manager rows buffered at once

// ---------------------------------------------------------------------------
// Plugins (PLUGINS.md)
// ---------------------------------------------------------------------------
// Nine since 1.1.0, for lights, plus whatever the board profile compiles in
// (board.h: the panel on a board with a display). Exactly the table, so one
// more plugin fails registry.cpp's static_assert rather than compiling and
// never starting.
#define BBS_MAX_PLUGINS     (9 + BBS_BOARD_PLUGINS)   // compiled-in plugin table
#define BBS_PLUGIN_TICK_MS  250      // periodic hook cadence
// A PF_FAST plugin's cadence: 50 frames a second for the lights, which is as
// fast as a pixel is worth updating and two loop passes apart.
#define BBS_PLUGIN_FAST_MS  20
#define BBS_PLUGIN_DIR      "p"      // <fs>/p/<name>/ holds a plugin's files
#define BBS_PLUGIN_QUOTA    65536    // per plugin, onboard
#define BBS_FS_RESERVE      32768    // free space the core keeps for accounts
// Heap kept free for callers and backups, which a plugin may not take at its
// start. A board profile may set less (board.h): with PSRAM, the callers'
// socket buffers and the backup inflater are PSRAM's, not internal RAM's.
#ifndef BBS_HEAP_RESERVE
#define BBS_HEAP_RESERVE    40960
#endif

// ---------------------------------------------------------------------------
// Scheduler / task
// ---------------------------------------------------------------------------
#define BBS_SELECT_MS       10       // loop cadence, also effect resolution
#define BBS_TASK_CORE       1        // Wi-Fi lives on core 0
// Bytes, not words: StackType_t is uint8_t in the IDF's Xtensa port, and
// xTaskCreatePinnedToCore takes the depth in StackType_t units. It comes out
// of the heap when the task is created (pvPortMallocStack), not out of static
// DRAM, so raising it costs heap one for one and nothing against the 180,736.
// 12,288 since 1.1.0: 8,192 had reached 1,440 free on 0.22.1 by a path SYS
// could not name, and the UserRec scratch buffers moving onto it as locals
// add up to three records (1,476 bytes) on an account write.
#define BBS_TASK_STACK      12288
#define BBS_TASK_PRIO       5

// The band of the task's stack that stackWatch reads just under the low mark
// after each phase (plat::stackDeeper), in bytes. 512 because a UserRec is
// 492: on the first host run a sign-up went 1,296 bytes deeper in one step
// through a frame holding a record that path never touches, stepped clean
// over a 256 byte band, and was only caught by the once-a-second full read,
// without its phase. 128 word reads, a few microseconds a check.
#define BBS_STACK_BAND      512

// A pass slower than this gets a line on the console naming which phase ate
// it. SYS has always shown "Loop worst" as a bare number, which says a stall
// happened and never says where, so every investigation started by guessing.
// 50 ms is five loop cadences: long enough that nothing healthy trips it,
// short enough to catch a stall well before it is one a caller notices.
#define BBS_SLOW_PASS_US    50000u

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
// The last Wi-Fi network this board joined, on userdata (1.1.0). A record the
// board keeps about its own radio, not a setting: not in system.cfg, not on
// a CONFIG page, not in the backup zip. See core/recovery.h.
#define BBS_WIFI_LAST_FILE  "wifi.last"
// The BOOT button, for the BOOT-hold reset. GPIO0 on every ESP32 and ESP32-S3
// board, because it is the strapping pin that selects download mode, which
// is also why the hold only counts once the firmware is running.
#define BBS_BOOT_GPIO       0
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
// Where a restore is unpacked and checked before anything goes live: on
// userdata since 1.1.0 (<userdata>/.staging), the 608 KB partition, rather
// than on storage, the 256 KB one the screens themselves fill a good part of.
// It also puts system.cfg, users.txt and the information pages on the same
// partition as the files they replace, so each one goes live with a rename;
// a rename from storage to userdata is refused (EXDEV) on the board.
#define BBS_BACKUP_STAGING      ".staging"

// Upload limits (enforced, documented in SCREENS.md). 256 KiB each since
// 1.1.0: 400,000 and 360,000 promised more than the partition the upload was
// staged on could ever hold, and it failed part way instead of being refused.
// A stored full backup of a board with 250 accounts is about 170 KiB. What
// actually decides is the room on the board at the time, which a restore
// measures before it unpacks anything (ziparc.cpp, roomCheck).
#define BBS_ZIP_MAX_BYTES       262144   // the uploaded .zip itself
#define BBS_ZIP_MAX_FILES       64       // accepted files per upload
#define BBS_ZIP_FILE_MAX        65536    // one unpacked file
#define BBS_ZIP_USERS_MAX       163840   // users.txt: 100 full accounts with escaping
#define BBS_ZIP_TOTAL_MAX       262144   // all unpacked files together
#define BBS_SCREEN_NAME_MAX     8        // screen base name, a-z 0-9 _ -
// An information page in a backup (info/3.txt). The editor writes at most
// BBS_COMPOSE_MAX; this leaves room for one written on a laptop.
#define BBS_ZIP_INFO_MAX        8192
// LittleFS gives every file of more than a few hundred bytes at least one
// whole 4 KB block, so free space is counted in blocks when a restore asks
// whether it fits: 33 stock screens of 1 to 3 KB each take 33 blocks, not 9.
#define BBS_FS_BLOCK            4096

// Backups on the SD card (1.1.0): BACKUP SD, RESTORE SD, the nightly one.
#define BBS_CARD_STEP_BYTES     8192     // zip bytes written to the card per loop pass
#define BBS_RESTORE_ASK_MS      60000    // the sysop has this long to answer Y/N
#define BBS_NIGHTLY_HOUR        3        // local hour the nightly backup is made
