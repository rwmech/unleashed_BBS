// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/linkcrypto.h
// Module:       Core / the link's cryptography (1.2.0)
//
// Purpose:      The four things the µnleashed link needs from cryptography,
//               over mbedTLS, and nothing else:
//
//                 - AES-128-CCM with an 8-byte tag, to seal every session
//                   frame, the frame header as associated data (LINK.md,
//                   Frames);
//                 - HKDF-SHA256 (RFC 5869), to make keys: the pairing key
//                   from an ECDH secret, a session key at every HELLO;
//                 - ECDH on P-256, for pairing;
//                 - the 4-digit code a sysop may compare with the peer's
//                   console.
//
//               Why mbedTLS and not code of our own: the WROOM image already
//               links every one of these for WPA3 (checked in the 1.1.1 ELF),
//               so on the board it costs nearly no flash, and AES runs on the
//               ESP32's AES hardware (CONFIG_MBEDTLS_HARDWARE_AES). The host
//               build compiles the same mbedTLS 3.6.0 sources out of the
//               ESP-IDF package (host/Makefile, MBEDTLS_DIR), so the tests
//               run the code the board runs, in software.
//
//               HKDF is written here over the HMAC mbedTLS has, because
//               CONFIG_MBEDTLS_HKDF_C is off in the board's build and turning
//               it on for twenty lines is the wrong trade.
//
//               No state, no allocation of our own, no platform: the
//               satellite firmware builds this same file.
//
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1), the Linux host build, and
//               the link's peers (unleashed_camsat and door boxes)
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
#include <cstddef>
#include <cstdint>

namespace linkcrypto {

constexpr size_t kKey    = 16;   // AES-128
constexpr size_t kTag    = 8;    // CCM tag on every sealed frame
constexpr size_t kNonce  = 13;   // CCM nonce: dir, pn, 8 zero bytes
constexpr size_t kPub    = 65;   // P-256 public key, uncompressed
constexpr size_t kPriv   = 32;   // P-256 private scalar
constexpr size_t kSecret = 32;   // P-256 ECDH shared secret (x coordinate)

// A source of random bytes: the board's hardware RNG, or the host's.
// Returns 0 on success, as mbedTLS expects of its f_rng.
using Rng = int (*)(void* ctx, unsigned char* out, size_t n);

// nonce: the 13 bytes for one frame. dir is 'H' (host to peer) or 'P'.
void nonce(uint8_t out[kNonce], uint8_t dir, uint32_t pn);

// seal: encrypt n bytes of pt into ct (may be the same buffer) and write
// the 8-byte tag. ad is authenticated, not encrypted. False only if mbedTLS
// refused the key or the lengths.
bool seal(const uint8_t key[kKey], uint8_t dir, uint32_t pn,
          const uint8_t* ad, size_t adLen, const uint8_t* pt, size_t n,
          uint8_t* ct, uint8_t tag[kTag]);

// open: the reverse. False when the tag does not check, in which case ct
// may have been written with garbage and must not be used.
bool open(const uint8_t key[kKey], uint8_t dir, uint32_t pn,
          const uint8_t* ad, size_t adLen, const uint8_t* ct, size_t n,
          uint8_t* pt, const uint8_t tag[kTag]);

// hmac: HMAC-SHA256.
void hmac(const uint8_t* key, size_t keyLen, const uint8_t* in, size_t n, uint8_t out[32]);

// hkdf: RFC 5869 extract-and-expand, SHA-256, up to 255 * 32 bytes of out.
bool hkdf(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen,
          const uint8_t* info, size_t infoLen, uint8_t* out, size_t outLen);

// keypair: a fresh ephemeral P-256 key pair.
bool keypair(Rng rng, void* rctx, uint8_t priv[kPriv], uint8_t pub[kPub]);

// shared: the ECDH secret from our private key and their public key. False
// when their key is not a valid point on the curve, which is the check that
// stops an invalid-curve attack.
bool shared(Rng rng, void* rctx, const uint8_t priv[kPriv], const uint8_t theirPub[kPub],
            uint8_t out[kSecret]);

// wipe: clear key material in a way the compiler may not skip.
void wipe(void* p, size_t n);

}  // namespace linkcrypto
