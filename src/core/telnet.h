/*
 * File:        src/core/telnet.h
 * Description: Minimal telnet (RFC 854) filter. Strips and answers option
 *              negotiation, reads window size (NAWS), collapses CR LF and
 *              CR NUL into a single CR. Disabled for PETSCII callers who
 *              never spoke telnet, so the C64 pi key ($FF) passes through
 *              instead of being eaten as IAC.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include "term.h"

class Telnet {
public:
    void reset();

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled()     const { return enabled_; }
    bool clientSpoke() const { return seenIac_; }   // client sent IAC first

    // filter: raw socket bytes in, clean data bytes out (out >= n bytes).
    // Negotiation replies are written to reply. Returns data byte count.
    size_t filter(const uint8_t* in, size_t n, uint8_t* out, ByteSink& reply);

    // negotiate: server-side opening for character-at-a-time ANSI sessions
    void negotiate(ByteSink& reply);

    bool    hasSize() const { return cols_ != 0 && rows_ != 0; }
    uint8_t cols()    const { return cols_; }
    uint8_t rows()    const { return rows_; }

private:
    enum : uint8_t { S_DATA, S_IAC, S_OPT, S_SB, S_SB_IAC };

    void send3(ByteSink& o, uint8_t cmd, uint8_t opt);
    void onOption(uint8_t cmd, uint8_t opt, ByteSink& reply);
    void onSubneg();

    bool     enabled_ = true;
    bool     seenIac_ = false;
    bool     lastCR_  = false;
    uint8_t  st_      = S_DATA;
    uint8_t  cmd_     = 0;
    uint8_t  sb_[8]   = {};
    uint8_t  sbLen_   = 0;
    uint32_t us_      = 0;   // options we have agreed to (WILL)
    uint32_t him_     = 0;   // options we asked the client for (DO)
    uint8_t  cols_    = 0;
    uint8_t  rows_    = 0;
};
