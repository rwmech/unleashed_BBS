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
//                   the frame into the ring and returns, the send callback
//                   stores one flag;
//                 - send the next frame after the last one's callback, or
//                   the callbacks can come back out of order: linkRadioIdle;
//                 - a unicast needs the address in ESP-NOW's peer table, and
//                   so does broadcast (FF:FF:FF:FF:FF:FF);
//                 - channel 0 in a peer means "the channel the station is
//                   on", which is the router's: the link never sets one.
//
//               The ring is single producer (the Wi-Fi task) and single
//               consumer (the BBS loop), so two atomic indices are all the
//               locking it needs.
//
// Libraries:    ESP-IDF esp_wifi (ESP-NOW), esp_event, heap_caps
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

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"

namespace plat {

namespace {

// One received frame: 258 bytes a slot, so the default 8 slots are 2 KB.
struct Slot {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
};

Slot*                  g_ring = nullptr;
uint8_t                g_slots = 0;
std::atomic<uint16_t>  g_head{ 0 };    // written by the Wi-Fi task
std::atomic<uint16_t>  g_tail{ 0 };    // written by the loop
std::atomic<bool>      g_busy{ false };
std::atomic<uint32_t>  g_drops{ 0 };
std::atomic<uint32_t>  g_fails{ 0 };
std::atomic<uint32_t>  g_moves{ 0 };
std::atomic<uint8_t>   g_high{ 0 };
uint32_t               g_sentAt = 0;
bool                   g_up = false;
esp_event_handler_instance_t g_chanEvt = nullptr;

const uint8_t kBroadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!g_ring || !info || !data || len <= 0 || len > ESP_NOW_MAX_DATA_LEN) return;
    const uint16_t h = g_head.load(std::memory_order_relaxed);
    const uint16_t t = g_tail.load(std::memory_order_acquire);
    const uint16_t used = static_cast<uint16_t>(h - t);
    if (used >= g_slots) { g_drops.fetch_add(1); return; }
    Slot& s = g_ring[h % g_slots];
    memcpy(s.mac, info->src_addr, 6);
    s.rssi = info->rx_ctrl ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
    s.len = static_cast<uint8_t>(len);
    memcpy(s.data, data, static_cast<size_t>(len));
    g_head.store(static_cast<uint16_t>(h + 1), std::memory_order_release);
    if (used + 1 > g_high.load()) g_high.store(static_cast<uint8_t>(used + 1));
}

void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    (void)mac;
    if (status != ESP_NOW_SEND_SUCCESS) g_fails.fetch_add(1);
    g_busy.store(false);
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
    return e == ESP_OK || e == ESP_ERR_ESPNOW_EXIST;
}

}  // namespace

bool linkRadioStart(uint8_t slots) {
    if (g_up) return true;
    if (slots < 4) slots = 4;
    g_ring = static_cast<Slot*>(linkAlloc(sizeof(Slot) * slots));
    if (!g_ring) return false;
    g_slots = slots;
    g_head.store(0);
    g_tail.store(0);
    g_busy.store(false);
    g_high.store(0);
    if (esp_now_init() != ESP_OK) {
        linkFree(g_ring);
        g_ring = nullptr;
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
    linkFree(g_ring);
    g_ring = nullptr;
    g_slots = 0;
}

bool linkRadioUp() { return g_up; }

bool linkRadioSend(const uint8_t* mac, const uint8_t* f, size_t n) {
    if (!g_up || n > ESP_NOW_MAX_DATA_LEN) return false;
    const uint8_t* to = mac ? mac : kBroadcast;
    if (!ensurePeer(to)) return false;
    g_busy.store(true);
    g_sentAt = millis();
    if (esp_now_send(to, f, n) != ESP_OK) {
        g_busy.store(false);
        return false;
    }
    return true;
}

bool linkRadioIdle() {
    if (!g_busy.load()) return true;
    // A callback that never comes (the driver reset under us) must not stop
    // the link for ever: a frame is well under 10 ms of air at 1 Mbps.
    if (millis() - g_sentAt > 100) { g_busy.store(false); g_fails.fetch_add(1); return true; }
    return false;
}

size_t linkRadioRecv(uint8_t* out, size_t cap, uint8_t mac[6], int8_t& rssi) {
    if (!g_ring) return 0;
    const uint16_t t = g_tail.load(std::memory_order_relaxed);
    const uint16_t h = g_head.load(std::memory_order_acquire);
    if (t == h) return 0;
    const Slot& s = g_ring[t % g_slots];
    size_t n = s.len < cap ? s.len : cap;
    memcpy(out, s.data, n);
    memcpy(mac, s.mac, 6);
    rssi = s.rssi;
    g_tail.store(static_cast<uint16_t>(t + 1), std::memory_order_release);
    return n;
}

bool linkRadioAddPeer(const uint8_t mac[6]) { return g_up && ensurePeer(mac); }

void linkRadioDelPeer(const uint8_t mac[6]) {
    if (g_up && esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
}

uint8_t linkRadioChannel() {
    uint8_t ch = 0;
    wifi_second_chan_t sc = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&ch, &sc) != ESP_OK) return 0;
    return ch;
}

void linkRadioMac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

uint32_t linkRadioRingDrops()    { return g_drops.load(); }
uint8_t  linkRadioRingHigh()     { return g_high.load(); }
uint32_t linkRadioSendFails()    { return g_fails.load(); }
uint32_t linkRadioChannelMoves() { return g_moves.load(); }

void* linkAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void linkFree(void* p) { heap_caps_free(p); }

}  // namespace plat
