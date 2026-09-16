/*
 * File:        src/core/bus.h
 * Description: Message bus between sessions. Each session owns a fixed
 *              ring of BBS_BUS_DEPTH messages; posting never allocates.
 *              Messages are addressed by session, and the sender's handle
 *              travels as a display string only, so C2 user records can
 *              change what a handle is without changing this format.
 *              Delivery happens when the receiver is back at a prompt.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include "../config.h"

enum class BusKind : uint8_t {
    Page,        // PAGE n msg from another node
    Notice,      // arrivals and departures
    Broadcast,   // sysop to everyone
};

struct BusMsg {
    BusKind kind     = BusKind::Notice;
    uint8_t fromNode = 0;                        // 0 = sysop
    char    from[BBS_USER_MAX + 1] = {};
    char    text[BBS_LINE_MAX + 1] = {};
};

class Mailbox {
public:
    void clear() { head_ = count_ = 0; }
    bool empty() const { return count_ == 0; }

    // push: queue a message; when full the oldest is dropped (returns false)
    bool push(const BusMsg& m);

    // pop: oldest message first
    bool pop(BusMsg& out);

private:
    BusMsg  q_[BBS_BUS_DEPTH];
    uint8_t head_  = 0;
    uint8_t count_ = 0;
};
