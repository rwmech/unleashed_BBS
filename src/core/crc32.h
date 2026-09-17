/*
 * File:        src/core/crc32.h
 * Description: CRC-32 (IEEE 802.3, the ZIP/zlib polynomial), nibble table.
 *              Header-only so the core and the platform layer can share it.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include <cstddef>

namespace crc32 {

// update: feed bytes; start with crc = 0, the result is the final CRC
inline uint32_t update(uint32_t crc, const uint8_t* d, size_t n) {
    static const uint32_t kNib[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc = kNib[(crc ^ d[i]) & 0x0F] ^ (crc >> 4);
        crc = kNib[(crc ^ (d[i] >> 4)) & 0x0F] ^ (crc >> 4);
    }
    return ~crc;
}

} // namespace crc32
