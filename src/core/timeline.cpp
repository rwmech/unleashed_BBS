/*
 * File:        src/core/timeline.cpp
 * Description: Timed output ring buffer (see timeline.h).
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc only)
 */
#include "timeline.h"
#include <cstring>

static_assert(BBS_TL_FRAMES <= 255, "frame index is uint8_t");
static_assert(BBS_TL_BYTES  <= 65535, "byte index is uint16_t");

// ---------------------------------------------------------------------------
// openFrame: start a new empty frame at the tail
// ---------------------------------------------------------------------------
bool Timeline::openFrame() {
    if (fCount_ >= BBS_TL_FRAMES) return false;
    Frame& f = fr_[(fHead_ + fCount_) % BBS_TL_FRAMES];
    f.len   = 0;
    f.delay = 0;
    ++fCount_;
    return true;
}

// ---------------------------------------------------------------------------
// put: append bytes to the open (delay-free) tail frame
// ---------------------------------------------------------------------------
bool Timeline::put(const uint8_t* data, size_t len) {
    if (len == 0) return true;
    if (len > freeBytes()) return false;

    Frame* tail = fCount_ ? &fr_[(fHead_ + fCount_ - 1) % BBS_TL_FRAMES] : nullptr;
    if (!tail || tail->delay != 0 || (tail->len + len) > 0xFFFF) {
        if (!openFrame()) return false;
        tail = &fr_[(fHead_ + fCount_ - 1) % BBS_TL_FRAMES];
    }

    size_t pos   = (bHead_ + bCount_) % BBS_TL_BYTES;
    size_t first = BBS_TL_BYTES - pos;
    if (first > len) first = len;
    memcpy(buf_ + pos, data, first);
    if (len > first) memcpy(buf_, data + first, len - first);

    bCount_    = static_cast<uint16_t>(bCount_ + len);
    tail->len  = static_cast<uint16_t>(tail->len + len);
    return true;
}

// ---------------------------------------------------------------------------
// delay: hold before the next frame
// ---------------------------------------------------------------------------
void Timeline::delay(uint16_t ms) {
    if (ms == 0) return;
    Frame* tail = fCount_ ? &fr_[(fHead_ + fCount_ - 1) % BBS_TL_FRAMES] : nullptr;
    if (!tail || tail->delay != 0) {
        if (!openFrame()) return;
        tail = &fr_[(fHead_ + fCount_ - 1) % BBS_TL_FRAMES];
    }
    tail->delay = ms;
}

// ---------------------------------------------------------------------------
// clear: drop all pending output
// ---------------------------------------------------------------------------
void Timeline::clear() {
    bHead_ = bCount_ = 0;
    fHead_ = fCount_ = 0;
    waiting_ = false;
}

// ---------------------------------------------------------------------------
// skipDelays: remove every pending hold so output flushes at full speed
// ---------------------------------------------------------------------------
void Timeline::skipDelays() {
    for (uint8_t i = 0; i < fCount_; ++i) {
        fr_[(fHead_ + i) % BBS_TL_FRAMES].delay = 0;
    }
    waiting_ = false;
}

// ---------------------------------------------------------------------------
// pump: send everything that is due, honoring delays and emulated cps
// ---------------------------------------------------------------------------
int Timeline::pump(uint32_t now, SendFn send, void* ctx) {
    if (cps_) {
        uint32_t dt = now - lastPump_;
        if (dt > 1000) dt = 1000;
        budget_ += dt * cps_;
        uint32_t cap = cps_ * 100u;          // at most ~100 ms of burst
        if (cap < 1000) cap = 1000;          // always allow one char
        if (budget_ > cap) budget_ = cap;
    }
    lastPump_ = now;

    int total = 0;
    while (fCount_) {
        Frame& f = fr_[fHead_];

        if (f.len) {
            size_t chunk = BBS_TL_BYTES - bHead_;   // contiguous run
            if (chunk > f.len) chunk = f.len;
            if (cps_) {
                size_t allow = budget_ / 1000;
                if (allow == 0) break;
                if (chunk > allow) chunk = allow;
            }
            int n = send(ctx, buf_ + bHead_, chunk);
            if (n < 0) return -1;
            if (n == 0) break;
            bHead_  = static_cast<uint16_t>((bHead_ + n) % BBS_TL_BYTES);
            bCount_ = static_cast<uint16_t>(bCount_ - n);
            f.len   = static_cast<uint16_t>(f.len - n);
            total  += n;
            if (cps_) budget_ -= static_cast<uint32_t>(n) * 1000u;
            if (static_cast<size_t>(n) < chunk) break;   // socket full
            continue;
        }

        if (f.delay) {
            if (!waiting_) {
                waiting_   = true;
                waitUntil_ = now + f.delay;
            }
            if (static_cast<int32_t>(now - waitUntil_) < 0) break;
            waiting_ = false;
        }

        fHead_ = static_cast<uint8_t>((fHead_ + 1) % BBS_TL_FRAMES);
        --fCount_;
    }

    if (fCount_ == 0) bHead_ = 0;   // bCount_ is 0: realign the ring
    return total;
}
