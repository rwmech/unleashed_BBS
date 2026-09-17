/*
 * File:        src/core/sha256.h
 * Description: SHA-256 (FIPS 180-4), portable, no heap. Used for the user
 *              password hash (salted, repeated): simple on purpose, it
 *              keeps plaintext out of users.txt without stalling the loop.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include <cstddef>

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const uint8_t* data, size_t len);
    void finish(uint8_t out[32]);

    static void digest(const uint8_t* data, size_t len, uint8_t out[32]);

private:
    void block(const uint8_t* p);

    uint32_t h_[8];
    uint8_t  buf_[64];
    uint64_t total_;
    uint8_t  fill_;
};
