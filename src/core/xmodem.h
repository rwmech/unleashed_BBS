/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/xmodem.h
 * Module:       Core / XMODEM and YMODEM transfer engine
 *
 * Purpose:      XMODEM and YMODEM send and receive as a state machine that
 *                  knows nothing about sessions, sockets, files or the card.
 *                  Checksum and CRC-16, 128 byte and 1K blocks, both
 *                  directions. It is fed bytes, asked for bytes, and told
 *                  what time it is; it never blocks, never allocates and
 *                  owns no timer.
 *
 * Design:       One engine holds one 1K block (about 1.2 KB all in). The
 *               board keeps exactly one, because only one caller is in the
 *               file area at a time; nothing here is sized per node.
 *
 *               The data itself belongs to the owner. The engine asks for
 *               it through ReadFn and hands it back through WriteFn, so it
 *               never calls fopen and never learns what a file is.
 *
 *               YMODEM is here for one reason: the length. XMODEM has no
 *               length field, so its last block is padded out with SUB and
 *               a received file can be up to 127 or 1023 bytes longer than
 *               the original. This engine refuses to strip that padding,
 *               because 0x1A is a legal byte inside a C64 .PRG and a
 *               receiver that guesses truncates somebody's file. YMODEM's
 *               block 0 carries the name and the exact byte count, so the
 *               receiver stops at the last real byte and there is nothing
 *               left to guess about.
 *
 *               IAC escaping is deliberately NOT done here. This engine
 *               emits and consumes raw protocol bytes. A 0xFF in the data
 *               is doubled by the layer that owns the telnet socket, on the
 *               way out and on the way back in, exactly once. Doing it here
 *               as well would double it twice and corrupt every binary
 *               file, so do not add it to this file.
 *
 * Interfaces:   xmodem::Engine (beginSend, beginRecv, beginSendY, beginRecvY,
 *               feed, pull, tick, cancel, reset, status), xmodem::crc16,
 *               xmodem::sum8
 *
 * Libraries:    none (libc: memcpy, memset)
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     PLAN-FILES.md, README.md
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
#include <cstdint>
#include <cstddef>

namespace xmodem {

// ---------------------------------------------------------------------------
// The wire. Ward Christensen's control bytes, unchanged since 1977.
// ---------------------------------------------------------------------------
enum : uint8_t {
    SOH    = 0x01,   // a 128 byte block follows
    STX    = 0x02,   // a 1024 byte block follows (XMODEM-1K)
    EOT    = 0x04,   // that was the last block
    ACK    = 0x06,
    NAK    = 0x15,   // also the receiver's "start, checksum please"
    CAN    = 0x18,   // two in a row abort the transfer
    SUB    = 0x1A,   // pad byte in the final block
    CRCREQ = 0x43,   // 'C': the receiver's "start, CRC-16 please"
};

constexpr uint16_t kBlk128 = 128;
constexpr uint16_t kBlk1K  = 1024;

// The longest filename block 0 will carry, NUL included. A name that does
// not fit is refused rather than shortened: a file written under a name the
// caller did not choose is worse than a transfer that plainly did not start.
// 63 characters is far past anything a file area on a FAT card holds.
constexpr uint16_t kMaxName = 64;

// ---------------------------------------------------------------------------
// Timing. XMODEM's own numbers, and they are generous on purpose: a board
// that stops for a few milliseconds to read a card is nowhere near them.
// ---------------------------------------------------------------------------
constexpr uint32_t kStartPollMs    = 3000;    // receiver: gap between 'C'/NAK
constexpr uint32_t kBlockTimeoutMs = 10000;   // waiting for a block or an ACK
constexpr uint32_t kCharTimeoutMs  = 1000;    // gap allowed inside one block
// Three minutes, for the same reason: a caller downloading has to pick a
// folder to save into, and a sender that hangs up while they are still in
// the dialog is a transfer that never had a chance.
constexpr uint32_t kSenderWaitMs   = 180000;  // sender: for the receiver to begin
constexpr uint32_t kPurgeQuietMs   = 500;     // silence before NAKing a bad block

constexpr uint8_t  kMaxErrors      = 10;      // consecutive errors, then give up

// Waiting for a person, not for a machine.
//
// These were 3 polls before dropping CRC and kMaxErrors polls before giving
// up, which is 9 seconds and 30 seconds at kStartPollMs. Both are fine
// against a program, and both are far too short against somebody choosing a
// file in their terminal's dialog, which Rob puts at up to a full minute.
//
// What that cost: the board sent 'C', the caller went looking for a file,
// the board decided after 9 seconds that nobody could do CRC and switched
// to asking with NAK. The terminal had already committed to CRC-16 from
// that first 'C' and sent 133 byte blocks for ever after; the board was
// reading 132 and checking an 8-bit sum against the first CRC byte. Every
// block NAKed, every time, and both ends behaving exactly as written.
//
// lrzsz cannot show this because sz is adaptive: it follows the board down
// to checksum and the two agree. SyncTERM is not, and neither is most of
// what a caller will actually use.
//
// So: two full minutes of asking in CRC, then a minute of asking the old
// way for the genuinely ancient sender that only does checksums, then give
// up. The fallback is kept rather than deleted because a checksum-only
// sender is exactly the kind of machine this board exists for.
constexpr uint8_t  kCrcPolls       = 40;      // 'C's before falling back to NAK
constexpr uint8_t  kStartPolls     = 60;      // start polls before Err::NoStart
constexpr uint8_t  kFallbackErrors = 3;       // 1K failures before dropping to 128
constexpr uint8_t  kCanSeq         = 8;       // CANs sent to abort (2 is the minimum)

// ---------------------------------------------------------------------------
// crc16: CRC-16/XMODEM, polynomial 0x1021, initial value 0, no reflection,
// no final xor. Nibble table, the same shape as crc32.h.
// ---------------------------------------------------------------------------
uint16_t crc16(const uint8_t* d, uint16_t n);

// sum8: the classic 8-bit checksum, a plain sum of the block modulo 256
uint8_t sum8(const uint8_t* d, uint16_t n);

// ---------------------------------------------------------------------------
// ReadFn: the sender asking its owner for the next bytes of the file.
//   Put up to max bytes at dst and return how many. Return 0 for end of
//   file. Returning less than max is taken as "the file ends here", so the
//   engine calls again to be sure; a short read in the middle of a file is
//   therefore safe but wasteful.
//
// WriteFn: the receiver handing its owner one whole received block. Return
//   false to abort the transfer (out of space, write failed); the engine
//   then cancels and reports Err::SinkFailed. Under YMODEM with a length in
//   block 0 the last call is short, because the padding is cut off there.
//
// OpenFn: the YMODEM receiver handing its owner what block 0 said. The owner
//   opens the file when block 0 names it. Return false to refuse (bad name,
//   no room, already exists); the engine then cancels with Err::SinkFailed.
//   Called exactly once, before any WriteFn call.
// ---------------------------------------------------------------------------
using ReadFn  = uint16_t (*)(void* ctx, uint8_t* dst, uint16_t max);
using WriteFn = bool     (*)(void* ctx, const uint8_t* src, uint16_t len);
using OpenFn  = bool     (*)(void* ctx, const char* name, uint32_t size);

// ---------------------------------------------------------------------------
// Engine: one transfer, either direction.
//
// The owner's loop is always the same three calls, in this order:
//     eng.feed(rx, n, now);                    // bytes that arrived
//     eng.tick(now);                           // timeouts and retries
//     size_t n = eng.pull(tx, sizeof tx, now); // bytes to send
// and it keeps going while running(), then reads done() / failed().
//
// pull() must be called after feed() and tick(), because that is how an ACK
// or a NAK leaves the engine. An owner that stalls on pull() does not
// corrupt anything: the engine keeps only the newest control byte and the
// other end retries.
//
// The end of a file costs one extra exchange: the receiver NAKs the first
// EOT and believes the second, because an EOT is a single unprotected byte
// and a line hit between two blocks would otherwise truncate an upload and
// report success. A NAK after an EOT is the protocol working, not an error,
// and errors() does not count it.
//
// Padding, and why it is the caller's problem under XMODEM: XMODEM has no
// length field. The last block is filled out with SUB (0x1A), so a received
// file can carry up to 127 or 1023 bytes that were never in the original.
// This engine writes every byte it receives, padding included, and reports
// trailingPad() as advice. It does not strip anything, because 0x1A is a
// perfectly legal byte in a .PRG and a receiver that guesses is a receiver
// that truncates somebody's file.
//
// YMODEM is the answer to exactly that, and it is why it is in here. Block 0
// carries the filename and the file's length in bytes, so a YMODEM receiver
// hands WriteFn at most that many bytes in total and the file on the card is
// byte for byte the file that was sent. Nothing is guessed and nothing is
// stripped on a hunch. A sender that leaves the length out (a length of 0)
// falls back to the XMODEM behaviour, padding and all.
//
// Batch of one. YMODEM can carry several files in a row; this engine takes
// the first and refuses the rest, because a board sends one file at a time
// and a caller uploading three has two the sysop never asked for. The
// closing empty block 0 is still exchanged, so the far end sees a clean
// end of batch rather than a dropped line.
// ---------------------------------------------------------------------------
class Engine {
public:
    enum class Stat : uint8_t { Idle, Sending, Receiving, Done, Failed };

    enum class Err : uint8_t {
        None,
        NoStart,        // the other end never began
        Timeout,        // it began and then stopped answering
        TooManyErrors,  // kMaxErrors bad blocks in a row
        RemoteCancel,   // CAN CAN from the other end
        LocalCancel,    // our own cancel()
        Sequence,       // a block number that is neither the next nor the last
        SinkFailed,     // WriteFn said no
    };

    // -- starting ----------------------------------------------------------
    // beginSend: we send a file. allow1k offers 1024 byte blocks and falls
    // back to 128 if they keep failing. The receiver still decides checksum
    // or CRC by the character it starts us with.
    void beginSend(ReadFn fn, void* ctx, uint32_t now, bool allow1k = true);

    // beginRecv: we receive a file. wantCrc asks for CRC-16 first and falls
    // back to the checksum if the sender ignores kCrcPolls of them.
    void beginRecv(WriteFn fn, void* ctx, uint32_t now, bool wantCrc = true);

    // beginSendY: YMODEM send of one file. name and size go out in block 0,
    // and size must be what ReadFn will actually produce, because that is
    // the number the far end trims the padding against. The name is the bare
    // filename: it is copied as given, so a path in it is a path the caller
    // sees. An empty name would reach the far end as the end-of-batch header,
    // so the engine substitutes one rather than close a batch by accident.
    void beginSendY(ReadFn fn, void* ctx, const char* name, uint32_t size,
                    uint32_t now, bool allow1k = true);

    // beginRecvY: YMODEM receive. The name and size arrive in block 0 and are
    // handed to open() before any data. CRC-16 only, and deliberately: there
    // is no checksum YMODEM, and falling back to one mid-transfer against a
    // sender that has already spoken CRC would break a working line.
    void beginRecvY(OpenFn open, WriteFn write, void* ctx, uint32_t now);

    // cancel: abort from our side. Queues the CAN sequence, which the owner
    // must still pull() out; the transfer is Failed with Err::LocalCancel.
    void cancel();

    // reset: back to Idle, ready for another transfer
    void reset();

    // -- running -----------------------------------------------------------
    // feed: raw bytes from the other end. Returns how many were consumed,
    // which is all of them while the transfer runs and stops at the byte
    // that ended it. Anything left over belongs to the terminal again, not
    // to us, so the owner puts it back into the ordinary input path.
    size_t feed(const uint8_t* src, size_t len, uint32_t now);

    // pull: raw bytes for the other end, at most max. Returns the count.
    // Still drains after the transfer has ended, so the CAN sequence of an
    // abort actually leaves the board.
    size_t pull(uint8_t* dst, size_t max, uint32_t now);

    // tick: timeouts, retries and the receiver's start polling
    void tick(uint32_t now);

    // pending: pull() has bytes waiting
    bool pending() const;

    // -- state -------------------------------------------------------------
    bool running() const { return st_ == Stat::Sending || st_ == Stat::Receiving; }
    bool done()    const { return st_ == Stat::Done; }
    bool failed()  const { return st_ == Stat::Failed; }
    Stat status()  const { return st_; }
    Err  error()   const { return err_; }
    const char* errorText() const;

    // -- figures for a progress line ---------------------------------------
    // bytes(): payload moved. Padding is counted under XMODEM because it is
    // written; under YMODEM with a length it is not, so bytes() ends equal
    // to fileSize() and a progress line divides cleanly.
    uint32_t bytes()       const { return bytes_; }
    uint32_t blocks()      const { return blocks_; }   // blocks accepted
    uint16_t errors()      const { return errs_; }     // bad blocks and timeouts
    bool     crcMode()     const { return crc_; }
    uint16_t blockSize()   const { return blkLen_; }   // the block in hand
    // trailingPad(): under XMODEM, the run of SUB at the end and only a
    // guess. Under YMODEM with a length, the padding that was cut off, which
    // is not a guess at all.
    uint32_t trailingPad() const { return pad_; }

    // -- what block 0 said -------------------------------------------------
    bool        ymodem()   const { return ymodem_; }   // this transfer is YMODEM
    const char* fileName() const { return name_; }     // once block 0 is parsed
    uint32_t    fileSize() const { return ySize_; }    // 0: the sender did not say

private:
    // Phases. Send and receive never share one, which is what keeps the
    // CAN CAN check honest: it only ever runs in a phase where a data byte
    // cannot appear, so a 0x18 inside a file can never abort a transfer.
    // The YMODEM block 0 phases are their own rather than a flag on the
    // XMODEM ones, because block 0 is not file data: it must not advance
    // dataOff_, must not count towards bytes(), and is always 128 bytes even
    // when the rest of the file is going out in 1K blocks.
    enum class Ph : uint8_t {
        Idle,
        SendStart, SendData, SendAck, SendEot, SendEotAck,
        SendY0, SendY0Ack, SendY0Crc, SendYEndWait,
        RecvWait, RecvNum, RecvNumInv, RecvData, RecvCheck, RecvPurge,
    };

    void feedByte(uint8_t b, uint32_t now);
    void frameNext();                 // read and build the next block, or EOT
    void buildPkt();                  // header and check bytes for data_+dataOff_
    void refill();                    // top up data_ from ReadFn
    void retransmit();                // NAK or timeout on a sent block
    void blockDone(uint32_t now);     // a whole block arrived: check and place it
    void enterPurge(uint32_t now);    // drain the rest of a bad block first
    void countPad();                  // trailing SUB run of the received stream
    void queueCtl(uint8_t b, uint8_t n);
    void queuePair(uint8_t a, uint8_t b);   // ACK then 'C', which YMODEM needs
    void startY0(bool end);           // sender: build and arm block 0
    void recvHeader(uint32_t now);    // receiver: block 0 arrived and checked out
    void endOfFile(uint32_t now);     // receiver: the EOT handshake is finished
    void abortWith(Err e);            // CAN sequence out, then Failed
    void fail(Err e) { st_ = Stat::Failed; err_ = e; ph_ = Ph::Idle; }
    bool bumpError();                 // count one error, false when out of retries
    uint16_t pktLen() const { return static_cast<uint16_t>(3 + blkLen_ + chkLen_); }

    // timeUp: wrap-safe deadline test (millis() wraps every 49 days)
    static bool timeUp(uint32_t now, uint32_t at) {
        return static_cast<int32_t>(now - at) >= 0;
    }

    Stat    st_  = Stat::Idle;
    Err     err_ = Err::None;
    Ph      ph_  = Ph::Idle;

    ReadFn  read_  = nullptr;
    WriteFn write_ = nullptr;
    OpenFn  open_  = nullptr;
    void*   ctx_   = nullptr;

    bool    crc_     = false;   // CRC-16 rather than the 8-bit checksum
    bool    use1k_   = false;   // sender: still offering 1024 byte blocks
    bool    started_ = false;   // receiver: something has arrived
    bool    pktLive_ = false;   // a framed block is waiting to go out
    bool    eotSeen_ = false;   // receiver: an EOT arrived and was NAKed once
    bool    eotNak_  = false;   // sender: the receiver's one free NAK is spent
    bool    selfNak_ = false;   // sender: we resent on our own clock, so the
                                // next NAK is stale (see feedByte, SendAck)

    bool    ymodem_  = false;   // block 0, a name and a length
    bool    yHdr_    = false;   // receiver: the next block 0 is a header, not data
    bool    yEnd_    = false;   // that block 0 is the one that closes the batch
    bool    yFile_   = false;   // receiver: a file is open and being written

    uint8_t blkNum_  = 1;       // sender: block in flight; receiver: expected
    uint8_t rxNum_   = 0;       // receiver: block number of the block arriving
    uint8_t cans_    = 0;       // consecutive CANs seen
    uint8_t retries_ = 0;       // consecutive errors on the block in hand
    uint8_t polls_   = 0;       // receiver: start characters sent

    uint32_t deadline_   = 0;
    uint32_t lastRx_     = 0;   // last byte in, for the purge quiet timer
    uint32_t purgeStart_ = 0;

    uint32_t bytes_  = 0;
    uint32_t blocks_ = 0;
    uint32_t pad_    = 0;
    uint16_t errs_   = 0;

    uint32_t ySize_    = 0;     // the length out of block 0, 0 if unstated
    uint32_t yWritten_ = 0;     // receiver: how much of it has been handed on

    // Outgoing block, as three pieces so a 1K block is never copied: the
    // header, the data where it already sits, and the check bytes.
    uint8_t  hdr_[3]    = {};
    uint8_t  chkOut_[2] = {};
    uint8_t  chkLen_    = 0;
    uint16_t outPos_    = 0;

    uint8_t  ctl_[kCanSeq] = {};   // ACK / NAK / 'C' / EOT / the CAN sequence
    uint8_t  ctlLen_ = 0;
    uint8_t  ctlPos_ = 0;

    uint8_t  chkIn_[2] = {};       // receiver: check bytes as they arrive
    uint16_t got_      = 0;        // receiver: bytes of the block so far

    uint16_t dataLen_ = 0;         // sender: bytes held in data_
    uint16_t dataOff_ = 0;         // sender: where the block in hand starts
    uint16_t blkLen_  = 0;         // size of the block in hand

    // The filename, sent in block 0 or parsed out of it. It is a copy, so
    // the owner may hand beginSendY a name off its own stack.
    char     name_[kMaxName] = {};

    // The one block. Source data going out, landing area coming in.
    uint8_t  data_[kBlk1K] = {};
};

} // namespace xmodem
