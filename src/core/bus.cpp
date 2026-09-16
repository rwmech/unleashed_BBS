/*
 * File:        src/core/bus.cpp
 * Description: Per-session message ring (see bus.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#include "bus.h"

// ---------------------------------------------------------------------------
// push: append at the tail, overwrite the oldest when full
// ---------------------------------------------------------------------------
bool Mailbox::push(const BusMsg& m) {
    bool kept = true;
    if (count_ == BBS_BUS_DEPTH) {
        head_ = static_cast<uint8_t>((head_ + 1) % BBS_BUS_DEPTH);
        --count_;
        kept = false;
    }
    q_[(head_ + count_) % BBS_BUS_DEPTH] = m;
    ++count_;
    return kept;
}

// ---------------------------------------------------------------------------
// pop: remove from the head
// ---------------------------------------------------------------------------
bool Mailbox::pop(BusMsg& out) {
    if (!count_) return false;
    out   = q_[head_];
    head_ = static_cast<uint8_t>((head_ + 1) % BBS_BUS_DEPTH);
    --count_;
    return true;
}
