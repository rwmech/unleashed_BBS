/*
 * ===========================================================================
 *  µnleashed BBS
 *  Electronic freedom on a microcontroller.
 * ===========================================================================
 *
 * File:         src/main.cpp
 * Module:       Firmware entry point (ESP32)
 *
 * Purpose:      ESP32 entry point. NVS, LittleFS at /fs, system.cfg, Wi-Fi
 *                  station, then NTP + mDNS once online, then the BBS loop
 *                  pinned to core 1 (Wi-Fi runs on core 0).
 *
 * Interfaces:   app_main()
 *
 * Depends on:   core/bbs, core/sysconfig, platform, include/secrets.h
 *
 * Libraries:    ESP-IDF (esp_wifi, esp_netif, esp_event, nvs_flash, lwip
 *                  esp_sntp), joltwallet/littlefs, espressif/mdns
 * Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
 * See also:     README.md
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

#include "config.h"
#include "core/bbs.h"
#include "core/sysconfig.h"
#include "core/plugin.h"
#include "platform/platform.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.h.example to include/secrets.h and set Wi-Fi credentials"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_sntp.h"
#include "mdns.h"
#include "sdkconfig.h"
#include <cstring>
#include <ctime>

static const char* TAG = "main";
static EventGroupHandle_t s_wifi = nullptr;
static const EventBits_t WIFI_UP = BIT0;

// ---------------------------------------------------------------------------
// onNet: Wi-Fi / IP events. Reconnects forever on drop.
// ---------------------------------------------------------------------------
static void onNet(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* e = static_cast<wifi_event_sta_disconnected_t*>(data);
        xEventGroupClearBits(s_wifi, WIFI_UP);
        ESP_LOGW(TAG, "wifi down (reason %u, rssi %d), reconnecting",
                 static_cast<unsigned>(e->reason), static_cast<int>(e->rssi));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* e = static_cast<ip_event_got_ip_t*>(data);
        // Power save off, here, every time we come up.
        //
        // This used to sit next to esp_wifi_start(), which does not work and
        // was measured not working. Starting the station raises
        // WIFI_EVENT_STA_START, and the handler above answers it by calling
        // esp_wifi_connect() straight away, so the call on the next line was
        // racing association. Its return value was not checked either, so a
        // failure was silent, and nothing re-applied it after a reconnect,
        // which with CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE leaves the
        // board on the IDF default of WIFI_PS_MIN_MODEM.
        //
        // What that costs is not subtle. A station in MIN_MODEM sleeps
        // between DTIM beacons and the access point buffers for it, so a
        // packet arriving into a quiet moment waits for the next beacon.
        // Measured on the live board with no callers at all: a median ping
        // of 13 ms, a 90th percentile of 1003 ms, and seventeen of the slow
        // samples inside a 31 ms window at exactly 1.00 s. A cluster that
        // tight on a round number is a timer, not interference. Keep the
        // board busy and every spike disappears.
        //
        // It is also why this looked like a BBS bug for so long: the stall
        // lands on whoever pauses to read and then types, and DASH 1 sends a
        // frame and then goes deliberately quiet for a second, which is
        // precisely the gap that lets the radio doze.
        esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ps != ESP_OK) {
            ESP_LOGW(TAG, "could not turn Wi-Fi power save off (%s); "
                          "expect about a second of delay on an idle link",
                     esp_err_to_name(ps));
        }
        ESP_LOGI(TAG, "online " IPSTR "  dial in: telnet " IPSTR " %u",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip), BBS_PORT);
        xEventGroupSetBits(s_wifi, WIFI_UP);
    }
}

// ---------------------------------------------------------------------------
// copyField: bounded copy into a fixed Wi-Fi config field
// ---------------------------------------------------------------------------
static void copyField(uint8_t* dst, size_t cap, const char* src) {
    size_t n = strlen(src);
    if (n > cap) n = cap;
    memcpy(dst, src, n);
}

// ---------------------------------------------------------------------------
// wifiStart: station mode, credentials from secrets.h
// ---------------------------------------------------------------------------
static void wifiStart() {
    s_wifi = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* nif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(nif, syscfg::get().hostname);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onNet, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onNet, nullptr);

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    copyField(wc.sta.ssid, sizeof(wc.sta.ssid), WIFI_SSID);
    copyField(wc.sta.password, sizeof(wc.sta.password), WIFI_PASS);
    wc.sta.threshold.authmode = strlen(WIFI_PASS) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    // several APs share one SSID: scan every channel, join the strongest
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Power save is NOT set here. esp_wifi_start() raises STA_START, whose
    // handler connects immediately, so anything on this line races the
    // association. It is applied in the IP_EVENT_STA_GOT_IP handler instead,
    // where it also gets re-applied after every reconnect. See onNet.
}

// ---------------------------------------------------------------------------
// fsMount: one LittleFS partition. "storage" holds the screens, and is the
// one a filesystem upload replaces; "userdata" holds the accounts, the live
// config and each plugin's files, which survive a reflash; "logs" holds
// fixed-size log rings only.
// ---------------------------------------------------------------------------
static void fsMount(const char* label, const char* base) {
    esp_vfs_littlefs_conf_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.base_path              = base;
    conf.partition_label        = label;
    conf.format_if_mount_failed = true;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "littlefs %s mount failed: %s", label, esp_err_to_name(err));
        return;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(label, &total, &used);
    ESP_LOGI(TAG, "littlefs %s at %s: %u of %u bytes used", label, base,
             static_cast<unsigned>(used), static_cast<unsigned>(total));
}

// ---------------------------------------------------------------------------
// onTimeSync: first NTP answer (and every resync) lands here
// ---------------------------------------------------------------------------
static void onTimeSync(struct timeval*) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
    plat::log("ntp: clock set %s", buf);
}

// ---------------------------------------------------------------------------
// netServicesStart: SNTP poll client and mDNS responder, once per boot.
// mDNS follows the station interface through reconnects on its own.
// ---------------------------------------------------------------------------
static void netServicesStart() {
    const SysConfig& cfg = syscfg::get();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, cfg.ntpServer);          // static storage, outlives SNTP
    sntp_set_time_sync_notification_cb(onTimeSync);
    esp_sntp_init();

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set(cfg.hostname);
    mdns_instance_name_set(BBS_NAME);
    mdns_service_add(BBS_NAME, "_telnet", "_tcp", BBS_PORT, nullptr, 0);
    ESP_LOGI(TAG, "mdns: %s.local, _telnet._tcp port %u", cfg.hostname, BBS_PORT);
}

// ---------------------------------------------------------------------------
// bbsTask: waits for the network, then runs the scheduler forever
// ---------------------------------------------------------------------------
static void bbsTask(void*) {
    xEventGroupWaitBits(s_wifi, WIFI_UP, pdFALSE, pdTRUE, portMAX_DELAY);
    netServicesStart();
    Bbs& bbs = Bbs::instance();
    while (!bbs.begin(BBS_PORT)) vTaskDelay(pdMS_TO_TICKS(1000));
    plugins::begin(bbs);

    // A second on the LED once the line is genuinely open. Wi-Fi being up is
    // not the same as the board being ready, and without a sign the only way
    // to find out is to dial in and be refused.
    plat::ledSignal(plat::millis(), 1000);

    for (;;) {
        bbs.tick();
        vTaskDelay(1);   // let lower-priority tasks on this core breathe
    }
}

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    plat::HeapStats h = plat::heap();
    plat::log("boot: %s %s  heap free %u  largest %u",
              BBS_NAME, BBS_VERSION, static_cast<unsigned>(h.freeBytes),
              static_cast<unsigned>(h.largestBlock));

    fsMount(BBS_FS_LABEL, BBS_FS_MOUNT);
    fsMount(BBS_USER_LABEL, BBS_USER_BASE);
    fsMount(BBS_LOGS_LABEL, BBS_LOGS_MOUNT);
    syscfg::load();          // hostname, TZ, NTP, staff passwords, limits, backup window
    wifiStart();

#if CONFIG_FREERTOS_UNICORE
    const BaseType_t core = 0;
#else
    const BaseType_t core = BBS_TASK_CORE;
#endif
    xTaskCreatePinnedToCore(bbsTask, "bbs", BBS_TASK_STACK, nullptr,
                            BBS_TASK_PRIO, nullptr, core);
}
