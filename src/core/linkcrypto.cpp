// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/linkcrypto.cpp
// Module:       Core / the link's cryptography (1.2.0)
//
// Purpose:      AES-128-CCM, HKDF-SHA256 and P-256 ECDH over mbedTLS 3.6.
//               See linkcrypto.h for why these and why mbedTLS.
//
// Libraries:    mbedTLS (ESP-IDF's component on the board; the same sources
//               compiled by host/Makefile on the host)
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1), the Linux host build, and
//               the link's peers
// See also:     LINK.md
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
#include "linkcrypto.h"

#include <cstring>

#include "mbedtls/aes.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

namespace linkcrypto {

void nonce(uint8_t out[kNonce], uint8_t dir, uint32_t pn) {
    memset(out, 0, kNonce);
    out[0] = dir;
    out[1] = static_cast<uint8_t>(pn);
    out[2] = static_cast<uint8_t>(pn >> 8);
    out[3] = static_cast<uint8_t>(pn >> 16);
    out[4] = static_cast<uint8_t>(pn >> 24);
}

// ---------------------------------------------------------------------------
// CCM (RFC 3610 / NIST SP 800-38C), built from two whole-buffer AES calls.
//
// mbedtls_ccm drives the cipher a block at a time, and on the ESP32 every
// block is one trip to the AES peripheral with its lock taken and given
// back: the bench measured about 320 us for one 222-byte frame on an ESP32
// and 1,000 us on an S3 (1.2.0, the camsat bench), against Rob's line of
// 100 us for a frame on the loop. The same arithmetic as one CBC call for
// the MAC and one CTR call for the keystream holds the peripheral once for
// each. The output is byte for byte mbedtls_ccm's: host/test_link.cpp checks
// it against mbedtls_ccm and against RFC 3610's packet vector 1.
//
// The frame's limits make the formatting fixed: a 13-byte nonce (so L = 2,
// a 2-byte length), an 8-byte tag, associated data under 65,280 bytes (a
// 2-byte length prefix), and a payload of at most kMaxSealed bytes.
// ---------------------------------------------------------------------------
namespace {

constexpr size_t kMaxSealed = 240;       // a 222-byte payload, with room
constexpr size_t kMaxAd     = 32;        // the link's is the 20-byte header

// mac: the CBC-MAC over B0, the associated data and the plaintext.
bool ccmMac(mbedtls_aes_context& aes, const uint8_t iv[kNonce], const uint8_t* ad, size_t adLen,
            const uint8_t* pt, size_t n, uint8_t mac[16]) {
    // B0 | len(ad) ad, zero-padded | plaintext, zero-padded
    uint8_t buf[16 + 16 + kMaxAd + kMaxSealed + 16];
    size_t k = 0;
    buf[k++] = static_cast<uint8_t>((adLen ? 0x40 : 0) | (((kTag - 2) / 2) << 3) | (2 - 1));
    memcpy(buf + k, iv, kNonce);
    k += kNonce;
    buf[k++] = static_cast<uint8_t>(n >> 8);
    buf[k++] = static_cast<uint8_t>(n);
    if (adLen) {
        buf[k++] = static_cast<uint8_t>(adLen >> 8);
        buf[k++] = static_cast<uint8_t>(adLen);
        memcpy(buf + k, ad, adLen);
        k += adLen;
        while (k % 16) buf[k++] = 0;
    }
    memcpy(buf + k, pt, n);
    k += n;
    while (k % 16) buf[k++] = 0;
    uint8_t chain[16] = {};
    const bool ok = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, k, chain, buf, buf) == 0;
    memcpy(mac, buf + k - 16, 16);
    mbedtls_platform_zeroize(buf, k);
    return ok;
}

// ctr: the keystream from counter 0, applied to [mac block | data]: the
// first block comes back as the tag, the rest as the other side's text.
bool ccmCtr(mbedtls_aes_context& aes, const uint8_t iv[kNonce], uint8_t* block, size_t len) {
    uint8_t ctr[16] = {};
    uint8_t stream[16];
    size_t off = 0;
    ctr[0] = 2 - 1;                        // flags: L - 1
    memcpy(ctr + 1, iv, kNonce);           // counter bytes 14-15 start at 0
    const bool ok = mbedtls_aes_crypt_ctr(&aes, len, &off, ctr, stream, block, block) == 0;
    mbedtls_platform_zeroize(stream, sizeof(stream));
    return ok;
}

}  // namespace

bool seal(const uint8_t key[kKey], uint8_t dir, uint32_t pn,
          const uint8_t* ad, size_t adLen, const uint8_t* pt, size_t n,
          uint8_t* ct, uint8_t tag[kTag]) {
    uint8_t iv[kNonce];
    nonce(iv, dir, pn);
    return ccmSeal(key, iv, ad, adLen, pt, n, ct, tag);
}

bool open(const uint8_t key[kKey], uint8_t dir, uint32_t pn,
          const uint8_t* ad, size_t adLen, const uint8_t* ct, size_t n,
          uint8_t* pt, const uint8_t tag[kTag]) {
    uint8_t iv[kNonce];
    nonce(iv, dir, pn);
    return ccmOpen(key, iv, ad, adLen, ct, n, pt, tag);
}

bool ccmSeal(const uint8_t key[kKey], const uint8_t iv[kNonce], const uint8_t* ad, size_t adLen,
             const uint8_t* pt, size_t n, uint8_t* ct, uint8_t tag[kTag]) {
    if (n > kMaxSealed || adLen > kMaxAd) return false;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    uint8_t block[16 + kMaxSealed];
    bool ok = mbedtls_aes_setkey_enc(&aes, key, 128) == 0 && ccmMac(aes, iv, ad, adLen, pt, n, block);
    if (ok) {
        memcpy(block + 16, pt, n);
        ok = ccmCtr(aes, iv, block, 16 + n);
    }
    if (ok) {
        memcpy(tag, block, kTag);
        memcpy(ct, block + 16, n);
    }
    mbedtls_platform_zeroize(block, sizeof(block));
    mbedtls_aes_free(&aes);
    return ok;
}

bool ccmOpen(const uint8_t key[kKey], const uint8_t iv[kNonce], const uint8_t* ad, size_t adLen,
             const uint8_t* ct, size_t n, uint8_t* pt, const uint8_t tag[kTag]) {
    if (n > kMaxSealed || adLen > kMaxAd) return false;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    uint8_t block[16 + kMaxSealed] = {};
    uint8_t mac[16];
    bool ok = mbedtls_aes_setkey_enc(&aes, key, 128) == 0;
    if (ok) {
        memcpy(block + 16, ct, n);         // block 0 stays zero: it comes back as S0
        ok = ccmCtr(aes, iv, block, 16 + n);
    }
    if (ok) ok = ccmMac(aes, iv, ad, adLen, block + 16, n, mac);
    if (ok) {
        // Constant time: which byte differed says nothing to somebody forging.
        uint8_t diff = 0;
        for (size_t i = 0; i < kTag; ++i) diff |= static_cast<uint8_t>((mac[i] ^ block[i]) ^ tag[i]);
        ok = diff == 0;
    }
    if (ok) memcpy(pt, block + 16, n);
    mbedtls_platform_zeroize(block, sizeof(block));
    mbedtls_platform_zeroize(mac, sizeof(mac));
    mbedtls_aes_free(&aes);
    return ok;
}

void sha256(const uint8_t* in, size_t n, uint8_t out[32]) {
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), in, n, out);
}

void hmac(const uint8_t* key, size_t keyLen, const uint8_t* in, size_t n, uint8_t out[32]) {
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keyLen, in, n, out);
}

bool hkdf(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen,
          const uint8_t* info, size_t infoLen, uint8_t* out, size_t outLen) {
    if (outLen > 255 * 32) return false;
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    static const uint8_t kZero[32] = {};
    uint8_t prk[32];
    // Extract. RFC 5869 2.2: no salt means a string of HashLen zeros.
    if (!salt || !saltLen) { salt = kZero; saltLen = sizeof(kZero); }
    mbedtls_md_hmac(md, salt, saltLen, ikm, ikmLen, prk);

    // Expand: T(i) = HMAC(PRK, T(i-1) | info | i), streamed so info can be
    // any length without a buffer of its own.
    mbedtls_md_context_t c;
    mbedtls_md_init(&c);
    bool ok = mbedtls_md_setup(&c, md, 1) == 0;
    uint8_t t[32];
    size_t tLen = 0, done = 0;
    for (uint8_t i = 1; ok && done < outLen; ++i) {
        ok = mbedtls_md_hmac_starts(&c, prk, sizeof(prk)) == 0 &&
             mbedtls_md_hmac_update(&c, t, tLen) == 0 &&
             (!infoLen || mbedtls_md_hmac_update(&c, info, infoLen) == 0) &&
             mbedtls_md_hmac_update(&c, &i, 1) == 0 &&
             mbedtls_md_hmac_finish(&c, t) == 0;
        tLen = sizeof(t);
        size_t take = outLen - done < tLen ? outLen - done : tLen;
        if (ok) memcpy(out + done, t, take);
        done += take;
    }
    mbedtls_md_free(&c);
    wipe(prk, sizeof(prk));
    wipe(t, sizeof(t));
    return ok;
}

bool keypair(Rng rng, void* rctx, uint8_t priv[kPriv], uint8_t pub[kPub]) {
    mbedtls_ecp_group g;
    mbedtls_mpi d;
    mbedtls_ecp_point q;
    mbedtls_ecp_group_init(&g);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&q);
    size_t olen = 0;
    bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecdh_gen_public(&g, &d, &q, rng, rctx) == 0 &&
              mbedtls_mpi_write_binary(&d, priv, kPriv) == 0 &&
              mbedtls_ecp_point_write_binary(&g, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, pub, kPub) == 0 &&
              olen == kPub;
    mbedtls_ecp_point_free(&q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&g);
    return ok;
}

bool shared(Rng rng, void* rctx, const uint8_t priv[kPriv], const uint8_t theirPub[kPub],
            uint8_t out[kSecret]) {
    mbedtls_ecp_group g;
    mbedtls_mpi d, z;
    mbedtls_ecp_point q;
    mbedtls_ecp_group_init(&g);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);
    mbedtls_ecp_point_init(&q);
    bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_mpi_read_binary(&d, priv, kPriv) == 0 &&
              mbedtls_ecp_point_read_binary(&g, &q, theirPub, kPub) == 0 &&
              mbedtls_ecp_check_pubkey(&g, &q) == 0 &&
              mbedtls_ecdh_compute_shared(&g, &z, &q, &d, rng, rctx) == 0 &&
              mbedtls_mpi_write_binary(&z, out, kSecret) == 0;
    mbedtls_ecp_point_free(&q);
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&g);
    return ok;
}

void wipe(void* p, size_t n) { mbedtls_platform_zeroize(p, n); }

}  // namespace linkcrypto
