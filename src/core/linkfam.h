// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/linkfam.h
// Module:       Core / the µnleashed link's message families (1.2.0)
//
// Purpose:      The CAMERA and DOOR families' message types and byte
//               layouts: the one place both the board and a peer (a camera
//               satellite, a door box) read them from, so the two cannot
//               drift. LINK.md carries the same tables for anybody writing a
//               peer in another language.
//
//               Every multi-byte field is little-endian. Text fields are
//               ASCII, padded with NUL to their width, and need not end in
//               one when full.
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1), the Linux host build, and
//               the link's peers
// See also:     LINK.md, src/core/link.h
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#pragma once
#include <cstdint>

namespace linkfam {

// ---------------------------------------------------------------------------
// Family 1: CAMERA
//
// Every picture is a host SNAP (camsat, 2026-09-26): the satellite never
// names or dates one. Motion, or a timer while it deep-sleeps, is a
// CAM_EVENT, which the host answers with a SNAP (reason CR_MOTION or
// CR_TIMELAPSE) under the Photos limits, or drops by not answering (hold-off,
// card full). The watermark and the picture correction run on the
// satellite, which has the PSRAM for them: the host sends the text.
// ---------------------------------------------------------------------------
enum : uint8_t {
    CAM_SNAP = 1,        // H->P  6 bytes, then the optional text block (kSnap*)
    CAM_PICTURE,         // P->H  bulk: the 16-byte picture header, then the JPEG
    CAM_SNAP_FAIL,       // P->H  3 bytes: u16 req, u8 code; CE_BUSY adds u8 place in
                         //       the queue (1.2.0: 1 = next, 0xFF = queue full)
    CAM_STATUS,          // P->H  28 bytes: u8 sensor, u8 maxSize, u32 heap, u32 psram,
                         //       u32 uptime, u8 lastErr, u8 pad, char model[12]
    CAM_EVENT,           // P->H  5 bytes: u8 kind (CEV_*), u32 takenAt (0: no clock)
    CAM_SETTINGS,        // H->P  24 bytes (kSet*)
    CAM_SETTINGS_OK,     // P->H  24 bytes: the same, as the satellite now runs it
};

// CAM_SNAP. The first 6 bytes are fixed; a satellite that stops reading at
// byte 6 still works (no watermark, no comment).
//   0 u16  req      the host's request id, echoed in the picture header
//   2 u8   size     CS_*
//   3 u8   quality  JPEG quality, 0: the satellite's default
//   4 u8   flash    CF_*
//   5 u8   reason   CR_*
//   6 u8   mark     1: stamp the board, date and who on the picture
//   7 char board[20]
//  27 char when[16] "2026-09-26 14:05", the host's local time
//  43 char who[24]  "*guest", a handle, "timelapse" or "motion"
//  67 comment       the JPEG COM text, up to 150 bytes, no NUL
constexpr uint8_t kSnapFixed   = 6;
constexpr uint8_t kSnapMark    = 6;
constexpr uint8_t kSnapBoard   = 7;
constexpr uint8_t kSnapWhen    = 27;
constexpr uint8_t kSnapWho     = 43;
constexpr uint8_t kSnapComment = 67;
constexpr uint8_t kSnapBoardN  = 20;
constexpr uint8_t kSnapWhenN   = 16;
constexpr uint8_t kSnapWhoN    = 24;
constexpr uint8_t kSnapCommentMax = 150;
constexpr uint8_t kSnapMax     = kSnapComment + kSnapCommentMax;   // 217
static_assert(kSnapMax <= 222, "a SNAP is one frame");

// CAM_SETTINGS and CAM_SETTINGS_OK, 24 bytes:
//   0 u16 timelapseMin  2 u8 timelapseSec  3 u8 motion (0/1)  4 u16 holdoffS
//   6 u8 size   7 u8 quality   8 u8 flash   9 u8 sleep (0 awake, 1 deep sleep
//   between shots)  10 u8 flip  11 u8 mirror  12 i8 bright  13 i8 contrast
//  14 i8 saturation  15 i8 exposure  16 u8 wb  17 u8 effect  18 u8 levels
//  19 u8 gammaIdx  20 u8 motionPin (0xFF none)
//  21 u8 recv: what the board sending it wants delivered (RECV_*), per
//     board (1.2.0, one satellite and several boards)
//  22 u8 in SETTINGS_OK: bit 0 set when the board it answers owns the
//     satellite; bytes 0 to 20 are taken only from the owner, and echo the
//     owner's to everyone
//  23 u8 pad
constexpr uint8_t kSettings = 24;
enum : uint8_t {
    kSetTimelapseMin = 0, kSetTimelapseSec = 2, kSetMotion = 3, kSetHoldoff = 4, kSetSize = 6,
    kSetQuality = 7, kSetFlash = 8, kSetSleep = 9, kSetFlip = 10, kSetMirror = 11, kSetBright = 12,
    kSetContrast = 13, kSetSaturation = 14, kSetExposure = 15, kSetWb = 16, kSetEffect = 17,
    kSetLevels = 18, kSetGamma = 19, kSetMotionPin = 20, kSetRecv = 21, kSetOwner = 22,
};
// kSetRecv bits. A board that has never said gets both (a satellite paired
// before 1.2.0 sent every picture to its one board).
// RECV_SAID marks a board that says: bytes 21 to 23 were padding before
// 1.2.0, so a 0 from an older board is not "wants nothing".
enum : uint8_t { RECV_TIMELAPSE = 1, RECV_MOTION = 2, RECV_ALL = 3, RECV_SAID = 0x80 };
// kSetOwner bits. SO_EVENTS: the satellite keeps its own timelapse clock and
// asks by EVENT (1.2.0), so the board's own clock leaves it alone.
enum : uint8_t { SO_OWNER = 1, SO_EVENTS = 0x80 };
// CE_BUSY's place in the queue when the queue is full
constexpr uint8_t kQueueFull = 0xFF;

// The picture header, the first 16 bytes of a CAM_PICTURE message:
//   0 u16 req      the SNAP's request id, 0 when the satellite started it
//   2 u8  reason   CR_*
//   3 u8  reserved 0
//   4 u16 width
//   6 u16 height
//   8 u32 takenAt  unix seconds, 0 when the satellite has no clock
//  12 u32 reserved 0
constexpr uint8_t kPictureHeader = 16;

// Frame sizes: the link's own numbers, not a camera driver's.
enum : uint8_t { CS_DEFAULT = 0, CS_QQVGA, CS_QVGA, CS_VGA, CS_SVGA, CS_XGA, CS_SXGA, CS_UXGA };
// Flash
enum : uint8_t { CF_OFF = 0, CF_ON, CF_AUTO };
// Why a picture was taken
enum : uint8_t { CR_CALLER = 1, CR_TIMELAPSE, CR_TEST, CR_MOTION };
// Why a snap failed
enum : uint8_t { CE_NOSENSOR = 1, CE_NOMEM, CE_BUSY, CE_FLASH, CE_CAPTURE };
// CAM_EVENT kinds
enum : uint8_t { CEV_MOTION = 1, CEV_TIMELAPSE };

// ---------------------------------------------------------------------------
// Family 2: DOOR
// ---------------------------------------------------------------------------
enum : uint8_t {
    DOOR_LIST = 1,       // P->H  u8 total sessions it holds, u8 count, then count x
                         //       (u8 id, u8 players, char name[24])
    DOOR_OPEN,           // H->P  u8 door id, then the handoff line (ASCII, no NUL)
    DOOR_OPEN_OK,        // P->H  empty
    DOOR_REFUSED,        // P->H  u8 DR_*, then up to 60 characters for the caller
    DOOR_DATA,           // both  bytes
    DOOR_RESIZE,         // H->P  u8 cols, u8 rows
    DOOR_WARN,           // H->P  u8 minutes left
    DOOR_TIMEUP,         // H->P  empty
    DOOR_FINISHED,       // P->H  u8 exit code, then up to 60 characters for the caller
    DOOR_CLOSE,          // H->P  u8 DC_*
    DOOR_LIST_ASK,       // H->P  empty: send DOOR_LIST on this session
};
constexpr uint8_t kDoorName = 24;
constexpr uint8_t kDoorsMax = 8;       // (222 - 2) / 26
enum : uint8_t { DR_FULL = 1, DR_UNKNOWN, DR_NOTNOW };
enum : uint8_t { DC_HUNGUP = 1, DC_TIMEUP, DC_TAKENBACK, DC_CLOSING };

}  // namespace linkfam
