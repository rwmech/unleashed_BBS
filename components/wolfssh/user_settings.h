/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         components/wolfssh/user_settings.h
 * Module:       Third party / wolfCrypt and wolfSSH build settings
 *
 * Purpose:      The one wolfCrypt 5.9.4 and wolfSSH 1.5.0 the SSH server
 *               (src/core/sshd.cpp, S3 only, 1.1.2) needs, and nothing
 *               else: a server with host keys made on the board,
 *               curve25519-sha256 and ecdh-sha2-nistp256 key exchange,
 *               ssh-ed25519 and ecdsa-sha2-nistp256 host keys (both, so
 *               SyncTERM on cryptlib and SyncTERM 1.10 each find a pair),
 *               AES-CTR and AES-GCM, HMAC-SHA2-256/512. No TLS, no RSA, no
 *               DH, no SHA-1, no SFTP or SCP. See
 *               internal/ssh-research-2026-09-26.md for every choice.
 *
 *               Software crypto only on the board: wolfCrypt's Espressif
 *               port drives the SHA and AES peripherals under its own
 *               mutexes, while the IDF's mbedTLS (the Wi-Fi supplicant)
 *               drives the same peripherals under another. Two locks on one
 *               peripheral (research section 6.2).
 *
 *               The same file builds the host's copy (host/Makefile), which
 *               keeps wolfSSH's client so the tests can call in with it.
 *
 * Targets:      ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
 * See also:     components/wolfssh/CMakeLists.txt, THIRD_PARTY_NOTICES.md
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

#if defined(ESP_PLATFORM)
  #include "sdkconfig.h"
  #define WOLFSSL_ESPIDF
  #define WOLFSSL_ESP32            /* the S3 is told apart by sdkconfig */
  /* No hardware crypto (above). */
  #define NO_ESP32_CRYPT
  #define NO_WOLFSSL_ESP32_CRYPT_HASH
  #define NO_WOLFSSL_ESP32_CRYPT_AES
  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI
  /* Every allocation PSRAM first, internal RAM only as a fallback:
   * XMALLOC, XFREE and XREALLOC are functions in src/core/sshd.cpp.
   * XMALLOC_USER, not XMALLOC_OVERRIDE: the FreeRTOS block in settings.h
   * quietly undoes the override (research section 3.2). */
  #define XMALLOC_USER
  /* The board's own server only. */
  #define NO_WOLFSSH_CLIENT
#endif
/* The host keeps wolfSSH's client: host/ssh_call.cpp is a caller. */

#define WOLFCRYPT_ONLY
#define WOLFSSL_WOLFSSH          /* wc_SSH_KDF */
#define NO_WRITEV
#define NO_MAIN_DRIVER
#define WOLFSSL_SMALL_STACK
#define WOLFSSL_NO_ASN_STRICT
#define WOLFSSL_IGNORE_FILE_WARN

/* Math: single precision, small code, P-256 only. */
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_SMALL
#define WOLFSSL_SP_NO_MALLOC
#define SP_INT_BITS 256
#define ECC_TIMING_RESISTANT
#define TFM_TIMING_RESISTANT

#define HAVE_ECC
#define ECC_USER_CURVES          /* P-256 only */
#undef  HAVE_ECC384
#undef  HAVE_ECC521
#define ECC_SHAMIR

#define HAVE_CURVE25519
#define CURVE25519_SMALL         /* settings.h forces it on Xtensa anyway */
#define HAVE_ED25519
#define ED25519_SMALL
#define WOLFSSL_ED25519_STREAMING_VERIFY
#define HAVE_ED25519_KEY_IMPORT
#define HAVE_ED25519_KEY_EXPORT

#define WOLFSSL_SHA512           /* Ed25519 needs it */
#define WOLFSSL_NOSHA512_224
#define WOLFSSL_NOSHA512_256
#define WOLFSSL_NO_SHA384_ALIAS
#define NO_SHA224

#define HAVE_AESGCM
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_DIRECT
#define NO_AES_192
#define GCM_SMALL

#define WOLFSSL_KEY_GEN          /* host keys made on the board at first start */
#define WOLFSSL_ASN_TEMPLATE

#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_MD4
#define NO_MD5
#define NO_RC4
#define NO_DES3
#define NO_PSK
#define NO_OLD_TLS
#define NO_PWDBASED
#define NO_SESSION_CACHE
#define NO_CERTS                 /* no X.509 user authentication */
#define NO_SHA                   /* nothing offered uses SHA-1 */

#define HAVE_HASHDRBG

/* wolfSSH. WOLFSSH_TERM with the filesystem layer left in (unused, the
 * linker drops it) is what compiles wolfSSH_SetTerminalResizeCb; with
 * WOLFSSH_SHELL as well, a window-change resizes (research section 5.3). */
#define WOLFSSH_TERM
#define WOLFSSH_SHELL
#define NO_TERMIOS
#define WOLFSSH_NO_SFTP
#define WOLFSSH_NO_SCP
/* An SSH "none" login reaches our callback, which lets a caller with no
 * account on the board into the ordinary login prompt (sshd.cpp). */
#define WOLFSSH_ALLOW_USERAUTH_NONE
