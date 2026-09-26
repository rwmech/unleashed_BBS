// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/platform/linkradio_esp32.cpp
// Module:       Platform / the µnleashed link's radio on ESP-NOW (1.2.0)
//
// Purpose:      linkradio.h on the ESP32 and S3, over ESP-IDF 5.3.1's ESP-NOW.
//
//               Checked against the pinned esp_now.h, not recalled:
//                 - 250 bytes a frame (ESP_NOW_MAX_DATA_LEN);
//                 - both callbacks run in the high-priority Wi-Fi task, "do
//                   not do lengthy operations": the receive callback copies
//                   the frame into a ring and returns, the send callback
//                   updates counters;
//                 - a unicast needs the address in ESP-NOW's peer table, and
//                   so does broadcast (FF:FF:FF:FF:FF:FF);
//                 - channel 0 in a peer means "the channel the station is
//                   on", which is the router's: the link never sets one;
//                 - esp_now_set_peer_rate_config sets a peer's PHY mode and
//                   rate, after esp_wifi_start and esp_now_init.
//
//               Bench (2026-09-26, the Waveshare S3 on Rob's router and an
//               ESP32-CAM): 802.11g 24 Mbps a peer, four frames outstanding,
//               bulk fragments drained by the runner, never the loop.
//
//               Each ring is single producer (the Wi-Fi task) and single
//               consumer (the loop for control, the runner for bulk), so two
//               atomic indices are all the locking it needs. Sizes are powers
//               of two, so the index arithmetic survives the counters wrapping.
//
// Libraries:    ESP-IDF esp_wifi (ESP-NOW), esp_event, heap_caps, FreeRTOS
// Targets:      ESP32 and ESP32-S3 (ESP-IDF 5.3.1)
// See also:     src/platform/linkradio.h, LINK.md
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>. The full
// text is in the LICENSE file at the top of this repository.
// ===========================================================================
#include "linkradio.h"
#include "platform.h"

#include <atomic>
#include <cstring>
#include <new>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace plat {

namespace {

// One received frame: 258 bytes a slot.
struct Slot {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
};

struct Ring {
    Slot*                 s = nullptr;
    uint16_t              mask = 0;        // slots - 1; slots is a power of two
    std::atomic<uint16_t> head{ 0 };       // the Wi-Fi task's
    std::atomic<uint16_t> tail{ 0 };       // the consumer's
    std::atomic<uint8_t>  high{ 0 };

    // put: in the Wi-Fi task. False when full.
    bool put(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
        if (!s) return false;
        const uint16_t h = head.load(std::memory_order_relaxed);
        const uint16_t t = tail.load(std::memory_order_acquire);
        const uint16_t used = static_cast<uint16_t>(h - t);
        if (used > mask) return false;
        Slot& x = s[h & mask];
        memcpy(x.mac, info->src_addr, 6);
        x.rssi = info->rx_ctrl ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
        x.len = static_cast<uint8_t>(len);
        memcpy(x.data, data, static_cast<size_t>(len));
        head.store(static_cast<uint16_t>(h + 1), std::memory_order_release);
        const uint16_t u = static_cast<uint16_t>(used + 1);
        if (u > high.load()) high.store(static_cast<uint8_t>(u > 255 ? 255 : u));
        return true;
    }
    // get: the consumer's. 0 when empty.
    size_t get(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi) {
        if (!s) return 0;
        const uint16_t t = tail.load(std::memory_order_relaxed);
        const uint16_t h = head.load(std::memory_order_acquire);
        if (t == h) return 0;
        const Slot& x = s[t & mask];
        const size_t n = x.len < cap ? x.len : cap;
        memcpy(out, x.data, n);
        memcpy(mac, x.mac, 6);
        rssi = x.rssi;
        tail.store(static_cast<uint16_t>(t + 1), std::memory_order_release);
        return n;
    }
    bool waiting() const { return s && head.load() != tail.load(); }
};

// A peer's rate: 24 Mbps; 1 Mbps after three MAC failures in a row; back
// after 30 s without one. On the heap with the rings (240 bytes the WROOM's
// static DRAM does not pay). Written only under the link lock; an entry is
// filled before the count that covers it is published, so the send
// callback can read the table without a lock. A deleted peer's entry is
// freed (MAC zeroed) and reused: the callback reading it mid-reuse can at
// worst count one failure against the wrong peer.
struct Rate {
    uint8_t               mac[6];
    std::atomic<uint8_t>  fails{ 0 };      // in a row
    std::atomic<uint32_t> lastFail{ 0 };   // millis
    bool                  slow = false;    // written under the link lock
    uint32_t              slowAt = 0;
};
constexpr uint8_t kRates = 12;             // 8 pairings, and devices asking to pair
Rate*                  g_rate = nullptr;
std::atomic<uint8_t>   g_nrate{ 0 };

Ring                   g_ctrl, g_bulk;
std::atomic<uint8_t>   g_inflight{ 0 };
std::atomic<uint32_t>  g_lastSent{ 0 };
std::atomic<uint32_t>  g_drops{ 0 };
std::atomic<uint32_t>  g_fails{ 0 };
std::atomic<uint32_t>  g_moves{ 0 };
bool                   g_up = false;
esp_event_handler_instance_t g_chanEvt = nullptr;
SemaphoreHandle_t      g_lock = nullptr;
void*                  g_mem = nullptr;

constexpr uint8_t  kOutstanding = 4;       // bench: +53% throughput at 24 Mbps
constexpr uint8_t  kFailsToSlow = 3;
constexpr uint32_t kCleanMs     = 30000;

const uint8_t kBroadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

uint16_t pow2(uint8_t n) {
    uint16_t p = 4;
    while (p < n && p < 128) p = static_cast<uint16_t>(p << 1);
    return p;
}

void inflightDown() {
    uint8_t n = g_inflight.load();
    while (n && !g_inflight.compare_exchange_weak(n, static_cast<uint8_t>(n - 1))) {}
}

Rate* rateOf(const uint8_t* mac) {
    if (!g_rate) return nullptr;
    const uint8_t n = g_nrate.load(std::memory_order_acquire);
    for (uint8_t i = 0; i < n; ++i)
        if (!memcmp(g_rate[i].mac, mac, 6)) return &g_rate[i];
    return nullptr;
}

bool setRate(const uint8_t* mac, bool fast) {
    esp_now_rate_config_t c = {};
    c.phymode = fast ? WIFI_PHY_MODE_11G : WIFI_PHY_MODE_11B;
    c.rate    = fast ? WIFI_PHY_RATE_24M : WIFI_PHY_RATE_1M_L;
    c.ersu    = false;
    c.dcm     = false;
    return esp_now_set_peer_rate_config(mac, &c) == ESP_OK;
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!info || !data || len <= 0 || len > ESP_NOW_MAX_DATA_LEN) return;
    // A bulk fragment (header bytes 10-11, nfrag, above 1) goes to the
    // runner's ring. Unauthenticated, so only a routing decision: the engine
    // checks the frame whichever ring it arrives on.
    const bool bulk = len >= 12 && (data[10] | (data[11] << 8)) > 1;
    if (!(bulk ? g_bulk : g_ctrl).put(info, data, len)) g_drops.fetch_add(1);
}

void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    inflightDown();
    Rate* r = mac ? rateOf(mac) : nullptr;
    if (status != ESP_NOW_SEND_SUCCESS) {
        g_fails.fetch_add(1);
        if (r) {
            r->fails.fetch_add(1);
            r->lastFail.store(millis());
        }
    } else if (r) {
        r->fails.store(0);
    }
}

void onChannel(void*, esp_event_base_t, int32_t, void* data) {
    const auto* e = static_cast<const wifi_event_home_channel_change_t*>(data);
    g_moves.fetch_add(1);
    if (e) log("link: the router moved from channel %u to %u", e->old_chan, e->new_chan);
}

bool ensurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;                     // the station's channel, which is the router's
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;                 // the link seals for itself
    const esp_err_t e = esp_now_add_peer(&p);
    if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) return false;
    if (memcmp(mac, kBroadcast, 6) != 0) {
        setRate(mac, true);
        static const uint8_t kFree[6] = {};
        Rate* r = rateOf(mac);
        if (!r) r = rateOf(kFree);                     // a forgotten peer's entry
        if (r) {
            r->fails.store(0);
            r->lastFail.store(0);
            r->slow = false;
            memcpy(r->mac, mac, 6);
        } else if (g_rate) {
            const uint8_t n = g_nrate.load();
            if (n < kRates) {
                Rate& x = g_rate[n];
                memcpy(x.mac, mac, 6);
                x.fails.store(0);
                x.lastFail.store(0);
                x.slow = false;
                g_nrate.store(static_cast<uint8_t>(n + 1), std::memory_order_release);
            }
        }
    }
    return true;
}

// adjustRate: down to 1 Mbps after three failures in a row, up again after
// 30 s clean. Called before each unicast, under the link lock.
void adjustRate(const uint8_t* mac) {
    Rate* r = rateOf(mac);
    if (!r) return;
    const uint32_t now = millis();
    if (!r->slow && r->fails.load() >= kFailsToSlow) {
        if (setRate(mac, false)) {
            r->slow = true;
            r->slowAt = now;
        }
    } else if (r->slow && now - r->slowAt >= kCleanMs && now - r->lastFail.load() >= kCleanMs) {
        if (setRate(mac, true)) {
            r->slow = false;
            r->fails.store(0);
        }
    }
}

void freeRings() {
    g_ctrl.s = g_bulk.s = nullptr;
    g_nrate.store(0);
    if (g_rate) for (uint8_t i = 0; i < kRates; ++i) g_rate[i].~Rate();
    g_rate = nullptr;
    linkFree(g_mem);
    g_mem = nullptr;
}

}  // namespace

bool linkRadioStart(uint8_t ctrlSlots, uint8_t bulkSlots) {
    if (g_up) return true;
    if (!g_lock) g_lock = xSemaphoreCreateRecursiveMutex();
    if (!g_lock) return false;
    const uint16_t nc = pow2(ctrlSlots), nb = pow2(bulkSlots);
    // The rate table first (it wants 4-byte alignment; a 258-byte Slot
    // does not keep it), then the two rings.
    g_mem = linkAlloc(sizeof(Rate) * kRates + sizeof(Slot) * (nc + nb));
    if (!g_mem) return false;
    g_rate = static_cast<Rate*>(g_mem);
    for (uint8_t i = 0; i < kRates; ++i) new (&g_rate[i]) Rate();
    g_ctrl.s = reinterpret_cast<Slot*>(static_cast<uint8_t*>(g_mem) + sizeof(Rate) * kRates);
    g_ctrl.mask = static_cast<uint16_t>(nc - 1);
    g_bulk.s = g_ctrl.s + nc;
    g_bulk.mask = static_cast<uint16_t>(nb - 1);
    Ring* const rings[] = { &g_ctrl, &g_bulk };
    for (Ring* r : rings) {
        r->head.store(0);
        r->tail.store(0);
        r->high.store(0);
    }
    g_inflight.store(0);
    g_nrate.store(0);
    if (esp_now_init() != ESP_OK) {
        freeRings();
        return false;
    }
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);
    ensurePeer(kBroadcast);
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, onChannel, nullptr, &g_chanEvt);
    g_up = true;
    return true;
}

void linkRadioStop() {
    if (!g_up) return;
    g_up = false;
    if (g_chanEvt) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, g_chanEvt);
        g_chanEvt = nullptr;
    }
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    freeRings();
}

bool linkRadioUp() { return g_up; }

bool linkRadioSend(const uint8_t* mac, const uint8_t* f, size_t n) {
    if (!g_up || n > ESP_NOW_MAX_DATA_LEN) return false;
    const uint8_t* to = mac ? mac : kBroadcast;
    if (!ensurePeer(to)) return false;
    if (mac) adjustRate(mac);
    g_inflight.fetch_add(1);
    g_lastSent.store(millis());
    if (esp_now_send(to, f, n) != ESP_OK) {
        inflightDown();
        return false;
    }
    return true;
}

bool linkRadioIdle() {
    if (g_inflight.load() < kOutstanding) return true;
    // Callbacks that never come (the driver reset under us) must not stop
    // the link for ever: four frames are a few ms of air even at 1 Mbps.
    if (millis() - g_lastSent.load() > 200) {
        g_inflight.store(0);
        g_fails.fetch_add(1);
        return true;
    }
    return false;
}

void linkRadioWait(uint32_t ms) {
    const uint32_t t0 = millis();
    do {
        vTaskDelay(1);
    } while (g_inflight.load() && millis() - t0 < ms);
}

size_t linkRadioRecv(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi) {
    return g_ctrl.get(out, cap, mac, rssi);
}

size_t linkRadioRecvBulk(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi) {
    return g_bulk.get(out, cap, mac, rssi);
}

bool linkRadioBulkWaiting() { return g_bulk.waiting(); }

bool linkRadioAddPeer(const uint8_t mac[6]) { return g_up && ensurePeer(mac); }

void linkRadioDelPeer(const uint8_t mac[6]) {
    if (!g_up) return;
    if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
    if (Rate* r = rateOf(mac)) memset(r->mac, 0, 6);   // free for the next peer
}

uint8_t linkRadioChannel() {
    uint8_t ch = 0;
    wifi_second_chan_t sc = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&ch, &sc) != ESP_OK) return 0;
    return ch;
}

bool linkRadioAssociated() {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

void linkRadioMac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

void linkLock() {
    if (g_lock) xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
}

void linkUnlock() {
    if (g_lock) xSemaphoreGiveRecursive(g_lock);
}

uint32_t linkRadioRingDrops()    { return g_drops.load(); }
uint8_t  linkRadioRingHigh()     { return g_ctrl.high.load(); }
uint8_t  linkRadioBulkHigh()     { return g_bulk.high.load(); }
uint32_t linkRadioSendFails()    { return g_fails.load(); }
uint32_t linkRadioChannelMoves() { return g_moves.load(); }

uint8_t linkRadioSlowPeers() {
    uint8_t n = 0;
    const uint8_t k = g_nrate.load(std::memory_order_acquire);
    for (uint8_t i = 0; i < k; ++i)
        if (g_rate[i].slow && esp_now_is_peer_exist(g_rate[i].mac)) ++n;
    return n;
}

void* linkAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void linkFree(void* p) { heap_caps_free(p); }

}  // namespace plat
