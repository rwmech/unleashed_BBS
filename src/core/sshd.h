/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/sshd.h
 * Module:       Core / the SSH server (1.1.2 preview, S3 only)
 *
 * Purpose:      SSH on the board's one port. The connect settle already
 *               waits for a telnet client's first bytes; a client whose
 *               first bytes are "SSH-2.0-" (RFC 4253 section 4.2: both sides
 *               send their identification at once) becomes an SSH link, and
 *               everything else carries on as telnet, unchanged. An SSH
 *               caller takes an ordinary node: SSH is a way in, not more
 *               lines.
 *
 *               wolfSSH 1.5.0 on wolfCrypt 5.9.4 runs on a task of its own
 *               (core 0, priority 2, internal stack), never in the loop:
 *               sshlink.h has the seam. Host keys (Ed25519 and ECDSA P-256)
 *               are made on the board at the first start and kept in
 *               <userdata>/ssh/; they are not in the backup zip, so a
 *               downloaded backup cannot make another board answer as this
 *               one.
 *
 *               Everything here is called from the BBS task.
 *
 * Libraries:    wolfSSH, wolfCrypt (components/wolfssh), the S3 build only
 * Targets:      ESP32-S3 (ESP-IDF 5.3.1) and the Linux host build
 * See also:     src/core/sshlink.h, internal/ssh-research-2026-09-26.md
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
#include "../config.h"

#if BBS_HAS_SSH
#include <cstddef>
#include <cstdint>
#include "sshlink.h"

namespace sshd {

// The identification a client sends first (RFC 4253 section 4.2). The loop
// matches it against the connect's first bytes.
constexpr char    kClientId[]  = "SSH-2.0-";
constexpr uint8_t kClientIdLen = sizeof(kClientId) - 1;

// begin: host keys (read, or made and written, both from this task, the
// only one that touches the flash), the eventfds, the task. False leaves
// SSH off with the reason in the log; telnet is not touched either way.
bool begin();
bool running();

// claim: hand a sniffed connection to the SSH task. sock becomes the task's;
// pre is what the loop had already read of it ("SSH-2.0-" and whatever
// followed in the same read). nullptr when no slot is free or the PSRAM a
// session needs is not there: the caller keeps the socket and refuses it
// (refusal), and the session is spent on nothing.
ssh::Link* claim(int sock, const uint8_t* pre, size_t n, uint32_t peer,
                 uint32_t local, uint8_t node);

// refusal: the bytes that tell an SSH client the SSH slots are full, with
// no key exchange: our identification line, then SSH_MSG_DISCONNECT
// (reason 12, too many connections) in the clear, which RFC 4253 allows
// before the keys exist. OpenSSH prints the description ("Received
// disconnect from ...: --> All SSH ports are full"), PuTTY shows it in its
// error box. why is the description, nullptr for the ports-full one.
// Returns the length written to out (about 80 bytes), 0 if cap is short.
size_t refusal(uint8_t* out, size_t cap, const char* why = nullptr);

// The loop's side of an open link.
size_t read(ssh::Link* l, uint8_t* d, size_t n);   // keys in, drains the wake too
int    write(ssh::Link* l, const uint8_t* d, size_t n);   // bytes out: how many taken
bool   gone(const ssh::Link* l);                   // the task has closed the socket
bool   open(const ssh::Link* l);                   // a shell channel is up
void   release(ssh::Link* l);                      // the session is done with it

// The authentication question, if one is waiting: which kind, and the user
// name (and password) the client sent. answer gives the loop's decision,
// wipes the password and wakes the task.
bool   asked(ssh::Link* l, uint8_t& kind, const char*& user, const char*& pass);
void   answer(ssh::Link* l, bool yes);
// wrongPasswords: a password was refused on this connection. One that ends
// without a password accepted counts once toward the address's ban
// (Bbs::closeSession); getting in as "none" afterwards does not undo it.
bool   wrongPasswords(const ssh::Link* l);
// refused: what the last claim that said no should tell the client.
const char* refused();
// why: the task's reason for ending the link, once gone(), else "".
const char* why(const ssh::Link* l);

// For SYS and HARDWARE (staff).
uint8_t     inUse();          // links not Free
uint8_t     lingering();      // sockets the task still holds for sessions already gone
uint8_t     cap();            // the lower of the board's constant and what PSRAM holds now
uint8_t     boardCap();       // BBS_SSH_MAX
const char* fingerprint(uint8_t which);   // 0 Ed25519, 1 ECDSA: "SHA256:...", "" none
const char* offWhy();         // why SSH is off, "" while it runs

} // namespace sshd
#endif  // BBS_HAS_SSH
