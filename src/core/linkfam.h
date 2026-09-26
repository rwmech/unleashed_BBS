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
// ---------------------------------------------------------------------------
enum : uint8_t {
    CAM_SNAP = 1,        // H->P  6 bytes: u16 req, u8 size, u8 quality, u8 flash, u8 reason
    CAM_PICTURE,         // P->H  bulk: the 16-byte picture header, then the JPEG
    CAM_SNAP_FAIL,       // P->H  3 bytes: u16 req, u8 code
    CAM_STATUS,          // P->H  28 bytes: u8 sensor, u8 maxSize, u32 heap, u32 psram,
                         //       u32 uptime, u8 lastErr, u8 pad, char model[12]
    CAM_EVENT,           // P->H  5 bytes: u8 kind, u32 takenAt
    CAM_SETTINGS,        // H->P  8 bytes: u16 timelapseMin, u8 motion, u16 holdoffS,
                         //       u8 size, u8 quality, u8 flash
    CAM_SETTINGS_OK,     // P->H  8 bytes: the same, as the satellite now runs
};

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
