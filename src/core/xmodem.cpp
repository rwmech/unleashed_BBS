/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/core/xmodem.cpp
 * Module:       Core / XMODEM transfer engine
 *
 * Purpose:      The XMODEM state machine (see xmodem.h). Checksum and
 *                  CRC-16, 128 byte and 1K blocks, send and receive, with
 *                  the protocol's own timeouts and retries. No heap, no
 *                  blocking, no timer of its own.
 *
 * Libraries:    none (libc: memset)
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

#include "xmodem.h"
#include <cstring>

namespace xmodem {

// ---------------------------------------------------------------------------
// crc16: CRC-16/XMODEM. Nibble table rather than 256 entries: 32 bytes of
// flash instead of 512, and still four times the speed of the bit loop.
// ---------------------------------------------------------------------------
uint16_t crc16(const uint8_t* d, uint16_t n) {
    static const uint16_t kNib[16] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
        0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    };
    uint16_t crc = 0;
    for (uint16_t i = 0; i < n; ++i) {
        crc = static_cast<uint16_t>((crc << 4) ^ kNib[((crc >> 12) ^ (d[i] >> 4)) & 0x0F]);
        crc = static_cast<uint16_t>((crc << 4) ^ kNib[((crc >> 12) ^ (d[i] & 0x0F)) & 0x0F]);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// sum8: the original checksum. Weak, and kept because half the 8-bit
// terminal programs in the world still start a transfer with NAK.
// ---------------------------------------------------------------------------
uint8_t sum8(const uint8_t* d, uint16_t n) {
    uint8_t s = 0;
    for (uint16_t i = 0; i < n; ++i) s = static_cast<uint8_t>(s + d[i]);
    return s;
}

// ===========================================================================
// Starting and stopping
// ===========================================================================

void Engine::reset() {
    st_  = Stat::Idle;
    err_ = Err::None;
    ph_  = Ph::Idle;

    read_  = nullptr;
    write_ = nullptr;
    ctx_   = nullptr;

    crc_ = use1k_ = started_ = pktLive_ = false;
    eotSeen_ = eotNak_ = false;

    blkNum_  = 1;
    rxNum_   = 0;
    cans_    = 0;
    retries_ = 0;
    polls_   = 0;

    deadline_ = lastRx_ = purgeStart_ = 0;
    bytes_ = blocks_ = pad_ = 0;
    errs_  = 0;

    chkLen_ = 0;
    outPos_ = 0;
    ctlLen_ = ctlPos_ = 0;
    got_    = 0;

    dataLen_ = dataOff_ = blkLen_ = 0;
    // data_ is left alone on purpose: a kilobyte of memset buys nothing,
    // every byte of it is written before it is read.
}

void Engine::beginSend(ReadFn fn, void* ctx, uint32_t now, bool allow1k) {
    reset();
    read_     = fn;
    ctx_      = ctx;
    use1k_    = allow1k;
    st_       = Stat::Sending;
    ph_       = Ph::SendStart;
    lastRx_   = now;
    deadline_ = now + kSenderWaitMs;
}

void Engine::beginRecv(WriteFn fn, void* ctx, uint32_t now, bool wantCrc) {
    reset();
    write_    = fn;
    ctx_      = ctx;
    crc_      = wantCrc;
    st_       = Stat::Receiving;
    ph_       = Ph::RecvWait;
    lastRx_   = now;
    deadline_ = now;        // the first start character goes out at once
}

void Engine::cancel() {
    if (!running()) return;
    abortWith(Err::LocalCancel);
}

// ---------------------------------------------------------------------------
// abortWith: tell the other end and stop. Eight CANs is what every DOS-era
// program sent; two in a row is all any of them needed to see.
// ---------------------------------------------------------------------------
void Engine::abortWith(Err e) {
    pktLive_ = false;
    queueCtl(CAN, kCanSeq);
    st_  = Stat::Failed;
    err_ = e;
    ph_  = Ph::Idle;
}

const char* Engine::errorText() const {
    switch (err_) {
        case Err::None:          return "";
        case Err::NoStart:       return "the other end never started";
        case Err::Timeout:       return "timed out";
        case Err::TooManyErrors: return "too many errors";
        case Err::RemoteCancel:  return "cancelled at the other end";
        case Err::LocalCancel:   return "cancelled";
        case Err::Sequence:      return "blocks out of sequence";
        case Err::SinkFailed:    return "could not store the file";
    }
    return "failed";
}

// ===========================================================================
// Outgoing bytes
// ===========================================================================

// ---------------------------------------------------------------------------
// queueCtl: hold a control byte (or the CAN run) for pull(). Anything the
// owner has not collected yet is dropped: a stalled owner should send the
// newest word, not a backlog the other end has stopped waiting for.
// ---------------------------------------------------------------------------
void Engine::queueCtl(uint8_t b, uint8_t n) {
    if (n > kCanSeq) n = kCanSeq;
    ctlPos_ = 0;
    ctlLen_ = 0;
    for (uint8_t i = 0; i < n; ++i) ctl_[ctlLen_++] = b;
}

bool Engine::pending() const {
    return ctlPos_ < ctlLen_ || (pktLive_ && outPos_ < pktLen());
}

size_t Engine::pull(uint8_t* dst, size_t max, uint32_t now) {
    size_t n = 0;
    while (n < max && ctlPos_ < ctlLen_) dst[n++] = ctl_[ctlPos_++];
    if (ctlPos_ >= ctlLen_) { ctlPos_ = 0; ctlLen_ = 0; }

    if (pktLive_) {
        const uint16_t total = pktLen();
        while (n < max && outPos_ < total) {
            const uint16_t p = outPos_++;
            dst[n++] = (p < 3)                              ? hdr_[p]
                     : (p < static_cast<uint16_t>(3 + blkLen_))
                                                            ? data_[dataOff_ + (p - 3)]
                                                            : chkOut_[p - 3 - blkLen_];
        }
        if (outPos_ >= total) {
            pktLive_ = false;
            // The ACK clock starts when the last byte of the block leaves,
            // not when the block was built: on a slow line those are not
            // the same moment and the difference is a false timeout.
            if (ph_ == Ph::SendData) {
                ph_       = Ph::SendAck;
                deadline_ = now + kBlockTimeoutMs;
            }
        }
    }

    if (ph_ == Ph::SendEot && ctlLen_ == 0 && !pktLive_) {
        ph_       = Ph::SendEotAck;
        deadline_ = now + kBlockTimeoutMs;
    }
    return n;
}

// ===========================================================================
// The sending side
// ===========================================================================

// ---------------------------------------------------------------------------
// refill: top up the block buffer from the owner. The loop is here so that
// a source which returns short reads (a card, a partition boundary) cannot
// quietly insert padding into the middle of a file.
// ---------------------------------------------------------------------------
void Engine::refill() {
    dataLen_ = 0;
    dataOff_ = 0;
    if (!read_) return;
    while (dataLen_ < kBlk1K) {
        const uint16_t want = static_cast<uint16_t>(kBlk1K - dataLen_);
        uint16_t got = read_(ctx_, data_ + dataLen_, want);
        if (!got) break;
        if (got > want) got = want;          // a callback that lies must not overrun
        dataLen_ = static_cast<uint16_t>(dataLen_ + got);
    }
}

// ---------------------------------------------------------------------------
// buildPkt: header and check bytes for the block sitting at dataOff_
// ---------------------------------------------------------------------------
void Engine::buildPkt() {
    hdr_[0] = (blkLen_ == kBlk1K) ? STX : SOH;
    hdr_[1] = blkNum_;
    hdr_[2] = static_cast<uint8_t>(0xFF - blkNum_);
    if (crc_) {
        const uint16_t c = crc16(data_ + dataOff_, blkLen_);
        chkOut_[0] = static_cast<uint8_t>(c >> 8);
        chkOut_[1] = static_cast<uint8_t>(c & 0xFF);
        chkLen_    = 2;
    } else {
        chkOut_[0] = sum8(data_ + dataOff_, blkLen_);
        chkLen_    = 1;
    }
}

// ---------------------------------------------------------------------------
// frameNext: the next block, or the end of the file
// ---------------------------------------------------------------------------
void Engine::frameNext() {
    if (dataOff_ >= dataLen_) {
        refill();
        if (dataLen_ == 0) {                 // end of file
            ph_      = Ph::SendEot;
            retries_ = 0;
            queueCtl(EOT, 1);
            return;
        }
    }

    const uint16_t rem = static_cast<uint16_t>(dataLen_ - dataOff_);
    blkLen_ = (use1k_ && rem >= kBlk1K) ? kBlk1K : kBlk128;
    if (rem < blkLen_) {
        // The tail of the file. Sending it as 128 byte blocks rather than
        // padding out a whole 1K one is what keeps the padding a caller has
        // to live with down to 127 bytes instead of 1023.
        memset(data_ + dataOff_ + rem, SUB, static_cast<size_t>(blkLen_ - rem));
        dataLen_ = static_cast<uint16_t>(dataOff_ + blkLen_);
    }

    buildPkt();
    outPos_  = 0;
    pktLive_ = true;
    ph_      = Ph::SendData;
}

// ---------------------------------------------------------------------------
// retransmit: a NAK arrived, or the ACK never did
// ---------------------------------------------------------------------------
void Engine::retransmit() {
    if (!bumpError()) { abortWith(Err::TooManyErrors); return; }

    if (ph_ == Ph::SendEot || ph_ == Ph::SendEotAck) {
        ph_ = Ph::SendEot;
        queueCtl(EOT, 1);
        return;
    }

    // A 1K block that keeps failing is usually a receiver that cannot do 1K
    // at all, or a line too dirty to carry one intact. Reframe the same
    // block number as 128 bytes and stay small for the rest of the file.
    // The data is untouched in the buffer, so nothing has to be re-read.
    if (use1k_ && blkLen_ == kBlk1K && retries_ >= kFallbackErrors) {
        use1k_  = false;
        blkLen_ = kBlk128;
        buildPkt();
    }
    outPos_  = 0;
    pktLive_ = true;
    ph_      = Ph::SendData;
}

// ===========================================================================
// The receiving side
// ===========================================================================

// ---------------------------------------------------------------------------
// countPad: the run of SUB bytes at the end of what has arrived. Advice
// only: XMODEM cannot tell padding from a file that really ends in 0x1A.
// ---------------------------------------------------------------------------
void Engine::countPad() {
    uint16_t run = 0;
    while (run < blkLen_ && data_[blkLen_ - 1 - run] == SUB) ++run;
    pad_ = (run == blkLen_) ? pad_ + run : run;
}

// ---------------------------------------------------------------------------
// enterPurge: a block failed its check. Let the rest of it arrive and go
// quiet before NAKing, or the NAK lands mid-block and the retransmission
// arrives glued to the tail of the block that was already wrong.
// ---------------------------------------------------------------------------
void Engine::enterPurge(uint32_t now) {
    if (!bumpError()) { abortWith(Err::TooManyErrors); return; }
    ph_         = Ph::RecvPurge;
    purgeStart_ = now;
    lastRx_     = now;
}

// ---------------------------------------------------------------------------
// blockDone: a whole block is in. Check it, place it, answer it.
// ---------------------------------------------------------------------------
void Engine::blockDone(uint32_t now) {
    const bool ok = crc_
        ? (crc16(data_, blkLen_) == static_cast<uint16_t>((chkIn_[0] << 8) | chkIn_[1]))
        : (sum8(data_, blkLen_) == chkIn_[0]);
    if (!ok) { enterPurge(now); return; }

    if (rxNum_ == blkNum_) {
        if (write_ && !write_(ctx_, data_, blkLen_)) { abortWith(Err::SinkFailed); return; }
        bytes_ += blkLen_;
        ++blocks_;
        countPad();
        ++blkNum_;                       // wraps at 255, which is the protocol
        retries_ = 0;
        queueCtl(ACK, 1);
    } else if (rxNum_ == static_cast<uint8_t>(blkNum_ - 1)) {
        // The block we already have. Our ACK was lost on the way back and
        // the sender is repeating itself: acknowledge it again and drop the
        // copy. Writing it a second time is the classic XMODEM bug and it
        // shows up as a file that is one block too long and corrupt from
        // there on.
        queueCtl(ACK, 1);
    } else {
        abortWith(Err::Sequence);
        return;
    }

    ph_       = Ph::RecvWait;
    deadline_ = now + kBlockTimeoutMs;
}

// ===========================================================================
// Incoming bytes
// ===========================================================================

bool Engine::bumpError() {
    ++errs_;
    ++retries_;
    return retries_ <= kMaxErrors;
}

size_t Engine::feed(const uint8_t* src, size_t len, uint32_t now) {
    size_t i = 0;
    while (i < len && running()) feedByte(src[i++], now);
    return i;          // whatever is left belongs to the terminal, not to us
}

void Engine::feedByte(uint8_t b, uint32_t now) {
    lastRx_ = now;

    switch (ph_) {

    // -- sending ----------------------------------------------------------
    case Ph::SendStart:
        // Whichever start character arrives decides the check for the whole
        // transfer. The receiver is in charge of that, not us.
        if (b == NAK || b == CRCREQ) {
            crc_  = (b == CRCREQ);
            cans_ = 0;
            frameNext();
        } else if (b == CAN) {
            if (++cans_ >= 2) fail(Err::RemoteCancel);
        } else {
            cans_ = 0;
        }
        break;

    case Ph::SendData:
        // Mid-block nothing else is worth hearing: an ACK cannot be for the
        // block still going out, and a NAK is for the one we are already
        // resending.
        if (b == CAN) { if (++cans_ >= 2) fail(Err::RemoteCancel); }
        else cans_ = 0;
        break;

    case Ph::SendAck:
        if (b == ACK) {
            cans_    = 0;
            bytes_  += blkLen_;
            ++blocks_;
            dataOff_ = static_cast<uint16_t>(dataOff_ + blkLen_);
            ++blkNum_;                   // wraps at 255, which is the protocol
            retries_ = 0;
            pktLive_ = false;
            frameNext();
        } else if (b == NAK) {
            cans_ = 0;
            retransmit();
        } else if (b == CAN) {
            if (++cans_ >= 2) fail(Err::RemoteCancel);
        } else {
            // Usually a second 'C': the receiver's start poll crossing our
            // first block on the wire. Resending on it is how a sender ends
            // up one ACK ahead of itself for the rest of the file, because
            // the receiver acknowledges the duplicate and that ACK is then
            // read as an ACK for the next block. Ignore it; if the block
            // really was lost, our own timeout resends it.
            cans_ = 0;
        }
        break;

    case Ph::SendEot:
        if (b == CAN) { if (++cans_ >= 2) fail(Err::RemoteCancel); }
        else cans_ = 0;
        break;

    case Ph::SendEotAck:
        if (b == ACK) { st_ = Stat::Done; ph_ = Ph::Idle; }
        else if (b == NAK) {
            cans_ = 0;
            // A receiver is supposed to NAK the first EOT (see the note on
            // the receiving side). That one is the protocol working, so it
            // does not count as an error and does not spend a retry.
            if (!eotNak_) {
                eotNak_ = true;
                ph_     = Ph::SendEot;
                queueCtl(EOT, 1);
            } else {
                retransmit();
            }
        }
        else if (b == CAN) { if (++cans_ >= 2) fail(Err::RemoteCancel); }
        else cans_ = 0;
        break;

    // -- receiving --------------------------------------------------------
    case Ph::RecvWait:
        if (b == SOH || b == STX) {
            blkLen_   = (b == STX) ? kBlk1K : kBlk128;
            started_  = true;
            cans_     = 0;
            got_      = 0;
            eotSeen_  = false;           // that EOT was a line hit after all
            ph_       = Ph::RecvNum;
            deadline_ = now + kCharTimeoutMs;
        } else if (b == EOT) {
            // The EOT is one unprotected byte, so the first one might be
            // line noise between two blocks rather than the end of the
            // file. Forsberg's reference has the sender resending an EOT
            // on anything that is not an ACK, which is what makes it safe
            // to NAK the first one and believe only the second. Taking the
            // first at its word truncates an upload and calls it a success.
            if (!eotSeen_) {
                eotSeen_  = true;
                queueCtl(NAK, 1);
                deadline_ = now + kBlockTimeoutMs;
            } else {
                queueCtl(ACK, 1);
                st_ = Stat::Done;
                ph_ = Ph::Idle;
            }
        } else if (b == CAN) {
            if (++cans_ >= 2) fail(Err::RemoteCancel);
        } else {
            cans_ = 0;                   // line noise between blocks
        }
        break;

    case Ph::RecvNum:
        rxNum_    = b;
        ph_       = Ph::RecvNumInv;
        deadline_ = now + kCharTimeoutMs;
        break;

    case Ph::RecvNumInv:
        // The block number is sent twice, the second time inverted. If the
        // pair does not agree the number cannot be trusted, and a block
        // placed under the wrong number is worse than a block lost.
        if (static_cast<uint8_t>(rxNum_ + b) != 0xFF) {
            enterPurge(now);
        } else {
            got_      = 0;
            ph_       = Ph::RecvData;
            deadline_ = now + kCharTimeoutMs;
        }
        break;

    case Ph::RecvData:
        data_[got_++] = b;
        deadline_     = now + kCharTimeoutMs;
        if (got_ >= blkLen_) { got_ = 0; ph_ = Ph::RecvCheck; }
        break;

    case Ph::RecvCheck:
        chkIn_[got_++] = b;
        deadline_      = now + kCharTimeoutMs;
        if (got_ >= (crc_ ? 2 : 1)) blockDone(now);
        break;

    case Ph::RecvPurge:
        break;                           // the tail of a block already lost

    case Ph::Idle:
        break;
    }
}

// ===========================================================================
// The clock
// ===========================================================================

void Engine::tick(uint32_t now) {
    if (!running()) return;

    switch (ph_) {

    case Ph::SendStart:
        if (timeUp(now, deadline_)) fail(Err::NoStart);
        break;

    case Ph::SendData:
    case Ph::SendEot:
        break;                           // waiting on pull(), not on the clock

    case Ph::SendAck:
    case Ph::SendEotAck:
        if (timeUp(now, deadline_)) retransmit();
        break;

    case Ph::RecvWait:
        if (!timeUp(now, deadline_)) break;
        if (!started_) {
            if (polls_ >= kMaxErrors) { fail(Err::NoStart); break; }
            // A sender that ignores 'C' may be checksum-only, which is half
            // the 8-bit world. Ask the old way rather than sit here.
            if (crc_ && polls_ >= kCrcPolls) crc_ = false;
            queueCtl(crc_ ? CRCREQ : NAK, 1);
            ++polls_;
            deadline_ = now + kStartPollMs;
        } else if (eotSeen_) {
            // We NAKed an EOT and nothing has been heard since. A sender
            // that does not resend an EOT it has already sent is sitting
            // waiting for an ACK it will never get, so take the end of the
            // file at its word rather than fail a complete transfer over a
            // handshake. A line-hit EOT does not reach here: a real sender
            // carries on with the next block, which clears the flag. And
            // this branch needs started_, so a stray EOT before the first
            // block can never be accepted as an empty file.
            queueCtl(ACK, 1);
            st_ = Stat::Done;
            ph_ = Ph::Idle;
        } else {
            if (!bumpError()) { abortWith(Err::Timeout); break; }
            queueCtl(NAK, 1);
            deadline_ = now + kBlockTimeoutMs;
        }
        break;

    case Ph::RecvNum:
    case Ph::RecvNumInv:
    case Ph::RecvData:
    case Ph::RecvCheck:
        // A block that stopped arriving part way through. The line is
        // already quiet, so there is nothing to purge: NAK and start again.
        if (timeUp(now, deadline_)) {
            if (!bumpError()) { abortWith(Err::Timeout); break; }
            queueCtl(NAK, 1);
            ph_       = Ph::RecvWait;
            deadline_ = now + kBlockTimeoutMs;
        }
        break;

    case Ph::RecvPurge:
        if (timeUp(now, lastRx_ + kPurgeQuietMs) ||
            timeUp(now, purgeStart_ + kBlockTimeoutMs)) {
            queueCtl(NAK, 1);
            ph_       = Ph::RecvWait;
            deadline_ = now + kBlockTimeoutMs;
        }
        break;

    case Ph::Idle:
        break;
    }
}

} // namespace xmodem
