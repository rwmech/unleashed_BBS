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
 *                  pinned to core 1 (Wi-Fi runs on core 0), under the task
 *                  watchdog. The BOOT-hold watch and the last good network
 *                  (core/recovery) are polled from here.
 *
 * Interfaces:   app_main()
 *
 * Depends on:   core/bbs, core/sysconfig, core/recovery, platform,
 *                  include/secrets.h
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
#include "core/bbs_util.h"
#include "core/improv.h"
#include "core/recovery.h"
#include "core/sysconfig.h"
#include "core/plugin.h"
#include "platform/platform.h"

// secrets.h is optional now. A developer's build may carry a network in it
// as a fallback; a published binary has none, and is told where to go by
// Improv over the cable it was flashed with. system.cfg wins over both.
// Never in a release (BBS_RELEASE, the esp32dev_release environment): a
// release built on a machine that has the file must still carry no network.
#if !defined(BBS_RELEASE) && __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_sntp.h"
#include "esp_task_wdt.h"
#include "mdns.h"
#include "sdkconfig.h"
#include <cstring>
#include <ctime>

static const char* TAG = "main";
static EventGroupHandle_t s_wifi = nullptr;
static const EventBits_t WIFI_UP   = BIT0;
static const EventBits_t SCAN_DONE = BIT1;

// What the radio is set to right now, and whether to keep dialling it.
// s_hold stops the disconnect handler reconnecting while Improv swaps the
// network over or scans: esp_wifi_set_config refuses while the station is
// connecting, and the handler would otherwise start a connect in the gap.
static char          s_ssid[33] = "";
static char          s_pass[65] = "";
static volatile bool s_hold     = false;
static esp_ip4_addr_t s_ip      = {};
// The port callers dial, as this boot listens on it. Read from system.cfg
// once, before anything announces it: the listener, mDNS, the console line
// and Improv's telnet URL all say this number. CONFIG can change the file,
// and the board moves at the next restart, never under a caller.
static uint16_t      s_port     = BBS_PORT;

// noSleep: keep the radio awake, and complain if it will not.
//
// Called on STA_CONNECTED and again on GOT_IP, because neither alone is
// enough. A first association raises both; a reconnect that keeps the same
// lease raises only the first, and the 0.18.0 fix hung on the second. A
// radio left dozing waits for the next DTIM beacon before it hears
// anything, measured on this AP as about a second.
static esp_err_t noSleep() {
    esp_err_t e = esp_wifi_set_ps(WIFI_PS_NONE);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "could not turn Wi-Fi power save off (%s); "
                      "expect about a second of delay on an idle link",
                 esp_err_to_name(e));
    }
    return e;
}

// ---------------------------------------------------------------------------
// onNet: Wi-Fi / IP events. Reconnects forever on drop.
// ---------------------------------------------------------------------------
static void onNet(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // No network set is a board waiting for Improv, not one to dial
        // nothing in a tight loop.
        if (s_ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(s_wifi, SCAN_DONE);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        // Associated, which is the earliest point the setting can be made to
        // stick. A reconnect that keeps the same lease never reaches the
        // GOT_IP branch below, and that is the hole the lag came back
        // through.
        noSleep();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* e = static_cast<wifi_event_sta_disconnected_t*>(data);
        xEventGroupClearBits(s_wifi, WIFI_UP);
        if (s_hold || !s_ssid[0]) return;       // Improv has the radio
        ESP_LOGW(TAG, "wifi down (reason %u, rssi %d), reconnecting",
                 static_cast<unsigned>(e->reason), static_cast<int>(e->rssi));
        // Asked again after the log line, which holds the console for a few
        // milliseconds: Improv may have taken the radio in between, and a
        // connect started now would dial the network it is replacing.
        if (!s_hold) esp_wifi_connect();
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
        esp_err_t ps = noSleep();
        (void)ps;
        s_ip = e->ip_info.ip;
        ESP_LOGI(TAG, "online " IPSTR "  dial in: telnet " IPSTR " %u",
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip), static_cast<unsigned>(s_port));
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
// wifiUse: point the station at a network. Remembers it in s_ssid/s_pass so
// the reconnect handler and Improv both know what the radio is set to.
// ---------------------------------------------------------------------------
static esp_err_t wifiUse(const char* ssid, const char* pass) {
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    copyField(wc.sta.ssid, sizeof(wc.sta.ssid), ssid);
    copyField(wc.sta.password, sizeof(wc.sta.password), pass);
    wc.sta.threshold.authmode = *pass ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    // several APs share one SSID: scan every channel, join the strongest
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e == ESP_OK) {
        snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
        snprintf(s_pass, sizeof(s_pass), "%s", pass);
    }
    return e;
}

// ---------------------------------------------------------------------------
// wifiStart: station mode. The network comes from system.cfg, then from
// secrets.h if this build has one, and otherwise from nobody yet: the board
// waits for Improv and says so on the console. s_wifi already exists:
// app_main makes it first, because Improv reads it from the first poll.
// ---------------------------------------------------------------------------
static void wifiStart() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* nif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(nif, syscfg::get().hostname);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onNet, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onNet, nullptr);

    const SysConfig& sc = syscfg::get();
    const char* ssid = sc.wifiSsid[0] ? sc.wifiSsid : WIFI_SSID;
    const char* pass = sc.wifiSsid[0] ? sc.wifiPass : WIFI_PASS;

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (wifiUse(ssid, pass) != ESP_OK) ESP_LOGE(TAG, "the Wi-Fi settings were refused by the radio");
    if (!s_ssid[0]) {
        ESP_LOGW(TAG, "no Wi-Fi network set. Send one over USB with Improv "
                      "(https://www.improv-wifi.com), or set wifi_ssid in system.cfg");
    } else {
        ESP_LOGI(TAG, "wifi: joining \"%s\" (from %s)", s_ssid,
                 sc.wifiSsid[0] ? "system.cfg" : "secrets.h");
    }
    // A network that has never joined here gets a minute, then the board
    // goes back to the last one that did (1.1.0, core/recovery). CONFIG
    // network saves untested, so a typo there used to take a board off the
    // air until somebody came with a cable.
    recovery::wifiBegin(plat::millis(), s_ssid, s_pass);
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
    mdns_service_add(BBS_NAME, "_telnet", "_tcp", s_port, nullptr, 0);
    ESP_LOGI(TAG, "mdns: %s.local, _telnet._tcp port %u", cfg.hostname, static_cast<unsigned>(s_port));
}

// ===========================================================================
// Improv Wi-Fi Serial on the console UART. The packets are core/improv; this
// is the radio and the wire. Polled from app_main between the steps of boot,
// then from the BBS task, before the network is up and for as long as the
// board runs, so a board that has moved house can be told its new network
// over the same cable.
//
// Why during boot (1.1.0). Opening the port in ESP Web Tools resets the
// board, and the installer's opening question has a deadline measured from
// then. Its dialog fetches the manifest and calls initialize(1500): it sends
// "request current state" at once, again every 1000 ms, and gives up at
// 1500 ms with "Improv Wi-Fi Serial not detected". A dialog that gives up
// has no firmware name or version to compare, so a board already running
// this firmware was offered a plain install rather than Update. The first
// poll used to wait for two partition walks and the radio's start; it now
// comes as soon as the settings are read, and the walks come after it.
// ===========================================================================
namespace imp {

constexpr uint32_t kTrialMs  = 30000;   // ESP Web Tools waits 45 s and says ~30
constexpr uint32_t kScanMs   = 15000;   // an all-channel scan takes 2-3 s; this is the backstop
constexpr uint8_t  kScanMax  = 20;      // networks listed; the page scrolls, a C64 does not care

improv::Parser    g_parser;
improv::JoinWatch g_join;               // the one unasked "provisioned" (see improv.h)
bool     g_uartOk = false;              // set by app_main once the driver is in
bool     g_radio  = false;              // wifiStart has run: scans and networks may start
bool     g_trial = false;               // a new network is being tried
uint32_t g_trialAt = 0;
bool     g_scanning = false;
uint32_t g_scanAt = 0;
char     g_oldSsid[33], g_oldPass[65];  // to go back to if the new one fails
char     g_newSsid[33], g_newPass[65];

bool up() { return xEventGroupGetBits(s_wifi) & WIFI_UP; }

// send: one frame, with a newline in front of it. The browser only looks
// for a packet at the start of a line, and the console may be part way
// through one. stdout's lock is held so a log line from another task cannot
// land in the middle of a packet: ESP_LOG takes the same lock, and a packet
// with a log line spliced into it fails its checksum and is never seen.
// uart_write_bytes rather than stdout itself, because stdout turns every
// 0x0A into CR LF and a length or checksum byte can be 0x0A.
// The IDF's newlib has no flockfile(), and its _flockfile macro does not
// compile as C++. __lock_acquire_recursive on the stream's own lock is what
// that macro expands to for a stream that is not a string, so this is the
// lock vfprintf takes and not a copy of it.
void send(const uint8_t* b, size_t n) {
    if (!n) return;
    __lock_acquire_recursive(stdout->_lock);
    fflush(stdout);
    uart_write_bytes(UART_NUM_0, "\n", 1);
    uart_write_bytes(UART_NUM_0, b, n);
    __lock_release_recursive(stdout->_lock);
}
void sendState(uint8_t st) { uint8_t b[16]; send(b, improv::stateFrame(b, sizeof(b), st)); }
void sendError(uint8_t e)  { uint8_t b[16]; send(b, improv::errorFrame(b, sizeof(b), e)); }
void sendResult(uint8_t cmd, const char* const* s, uint8_t n) {
    uint8_t b[improv::kDataMax + 16];
    send(b, improv::resultFrame(b, sizeof(b), cmd, s, n));
}

// sendUrl: where to go once the board is on the network. A telnet: link,
// because that is what this board is; a browser with a telnet handler
// opens it, and one without shows the address to type. Only ever as the
// answer to a question: the installer drops a result it did not ask for.
void sendUrl(uint8_t cmd) {
    char url[48];
    snprintf(url, sizeof(url), "telnet://" IPSTR ":%u", IP2STR(&s_ip), static_cast<unsigned>(s_port));
    const char* s[] = { url };
    sendResult(cmd, s, 1);
}

// switchTo: move the radio to another network and start dialling it. The
// set_config retries cover a connect already in flight, which refuses the
// change until it gives up; an all-channel scan can take a few seconds.
//
// That spin blocks the BBS loop, and it is only reached while the station
// is part way through a connect, which is to say off the network, which is
// to say with no callers on it to stall. A board that is up disconnects at
// once and set_config takes the first time.
bool switchTo(const char* ssid, const char* pass) {
    s_hold = true;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi, WIFI_UP);
    esp_err_t e = ESP_FAIL;
    for (int i = 0; i < 100; ++i) {
        e = wifiUse(ssid, pass);
        if (e != ESP_ERR_WIFI_STATE) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    s_hold = false;
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "improv: the radio refused \"%s\" (%s)", ssid, esp_err_to_name(e));
        return false;
    }
    return !s_ssid[0] || esp_wifi_connect() == ESP_OK;
}

void startScan() {
    if (g_scanning) return;
    if (g_trial) { sendError(improv::E_UNKNOWN); return; }
    // A board failing to join its network is dialling in a loop, and the
    // radio will not scan while it does. Stop dialling for the scan.
    s_hold = true;
    if (!up()) esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi, SCAN_DONE);
    esp_err_t e = ESP_FAIL;
    for (int i = 0; i < 100; ++i) {
        e = esp_wifi_scan_start(nullptr, false);
        if (e != ESP_ERR_WIFI_STATE) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (e != ESP_OK) {
        s_hold = false;
        if (!up() && s_ssid[0]) esp_wifi_connect();
        ESP_LOGW(TAG, "improv: scan refused (%s)", esp_err_to_name(e));
        sendError(improv::E_UNKNOWN);
        return;
    }
    g_scanning = true;
    g_scanAt   = plat::millis();
}

// endScan: give the radio back to the reconnect handler, whichever way the
// scan ended. Without it a scan that never finished would hold s_hold for
// ever: no reconnects, and every set-network refused as busy.
void endScan() {
    g_scanning = false;
    s_hold = false;
    if (!up() && s_ssid[0]) esp_wifi_connect();
}

// finishScan: one result per network, strongest first as the radio gives
// them, each name once, then an empty result to say that is all.
void finishScan() {
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    uint32_t seen[kScanMax];
    uint8_t  sent = 0;
    wifi_ap_record_t ap;
    for (uint16_t i = 0; i < n && sent < kScanMax; ++i) {
        if (esp_wifi_scan_get_ap_record(&ap) != ESP_OK) break;
        const char* name = reinterpret_cast<const char*>(ap.ssid);
        if (!name[0]) continue;                         // hidden network
        uint32_t h = bbsu::hash(name);
        bool dup = false;
        for (uint8_t k = 0; k < sent; ++k) if (seen[k] == h) dup = true;
        if (dup) continue;
        seen[sent++] = h;
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(ap.rssi));
        const char* s[] = { name, rssi, ap.authmode == WIFI_AUTH_OPEN ? "NO" : "YES" };
        sendResult(improv::C_SCAN, s, 3);
    }
    esp_wifi_clear_ap_list();                           // whatever was not read
    sendResult(improv::C_SCAN, nullptr, 0);
    endScan();
}

// cleanText: what system.cfg can hold and give back unchanged. The parser
// trims both ends and a line ends at a newline, so a value with a control
// byte or an outer space would join during the trial and fail after the
// reboot the save was for. An SSID may be UTF-8, so bytes above 0x7E are
// allowed there; a WPA2 passphrase is printable ASCII by the standard.
bool cleanText(const char* s, bool asciiOnly) {
    size_t n = strlen(s);
    if (n && (s[0] == ' ' || s[n - 1] == ' ')) return false;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < 0x20 || c == 0x7F) return false;
        if (asciiOnly && c > 0x7E) return false;
    }
    return true;
}

void handle() {
    if (g_parser.type() != improv::T_RPC) return;       // the board only answers questions
    uint8_t cmd = 0, plen = 0;
    const uint8_t* pay = nullptr;
    if (!improv::parseRpc(g_parser.data(), g_parser.len(), cmd, pay, plen)) {
        sendError(improv::E_INVALID);
        return;
    }
    switch (cmd) {
    case improv::C_STATE: {
        uint8_t st = improv::stateNow(g_trial, up());
        sendState(st);
        if (st == improv::S_PROVISIONED) sendUrl(improv::C_STATE);
        g_join.answered(st);            // already said: nothing to repeat unasked
        break;
    }
    case improv::C_INFO: {
        const SysConfig& c = syscfg::get();
        const char* s[] = { "unleashed BBS", BBS_VERSION, "ESP32",
                            c.boardName[0] ? c.boardName : c.hostname };
        sendResult(improv::C_INFO, s, 4);
        break;
    }
    // Before the radio is started (the one poll in app_main ahead of
    // wifiStart), a scan or a network cannot be started: busy, try again.
    case improv::C_SCAN:
        if (!g_radio) { sendError(improv::E_UNKNOWN); break; }
        startScan();
        break;
    case improv::C_WIFI: {
        if (!g_radio || g_trial || g_scanning) { sendError(improv::E_UNKNOWN); break; }
        if (!improv::parseWifi(pay, plen, g_newSsid, sizeof(g_newSsid), g_newPass, sizeof(g_newPass)) ||
            !cleanText(g_newSsid, false) || !cleanText(g_newPass, true)) {
            sendError(improv::E_INVALID);
            break;
        }
        snprintf(g_oldSsid, sizeof(g_oldSsid), "%s", s_ssid);
        snprintf(g_oldPass, sizeof(g_oldPass), "%s", s_pass);
        g_join.trialStarted();                          // the trial's answer says it now
        sendState(improv::S_PROVISIONING);
        plat::log("improv: trying \"%s\"", g_newSsid);  // never the password
        if (!switchTo(g_newSsid, g_newPass)) {
            sendError(improv::E_CONNECT);
            switchTo(g_oldSsid, g_oldPass);
            sendState(improv::S_AUTHORIZED);
            break;
        }
        g_trial   = true;
        g_trialAt = plat::millis();
        break;
    }
    default:
        sendError(improv::E_UNKNOWN_RPC);
        break;
    }
}

// finishTrial: the new network answered. Keep it in system.cfg, where it
// survives a reflash, and only then say so: a board that reported success
// and forgot at the next power cut would be worse than one that failed.
void finishTrial() {
    g_trial = false;
    syscfg::KeyVal kv[] = { { "wifi_ssid", g_newSsid }, { "wifi_password", g_newPass } };
    char err[80] = "";
    if (!syscfg::write(kv, 2, nullptr, err, sizeof(err))) {
        plat::log("improv: joined \"%s\" but could not save it: %s", g_newSsid, err);
        sendError(improv::E_UNKNOWN);
        return;
    }
    char rerr[80] = "";
    if (!syscfg::reload(rerr, sizeof(rerr))) plat::log("improv: saved, but %s", rerr);
    plat::log("improv: joined \"%s\" and saved it", g_newSsid);
    sendState(improv::S_PROVISIONED);
    sendUrl(improv::C_WIFI);
}

// joinedNew: up, and on the network being tried. WIFI_UP alone is not
// proof: a connect to the old network that slipped in as the radio changed
// hands, or a GOT_IP already queued, raises it too, and saving on that
// would write untested credentials and report success.
bool joinedNew() {
    if (!up()) return false;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    return !strcmp(reinterpret_cast<const char*>(ap.ssid), g_newSsid);
}

// poll: whatever has arrived, then the join, the trial and the scan.
void poll() {
    if (!g_uartOk) return;                  // the driver refused; say so once, at boot
    uint8_t buf[64];
    int n = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), 0);
    for (int i = 0; i < n; ++i) {
        if (g_parser.feed(buf[i]) != improv::Parser::Res::Packet) continue;
        g_join.heard();
        handle();
    }

    // The installer asked while the board was still joining, heard "not
    // provisioned", and is showing Connect to Wi-Fi. Say it once the board
    // is on: the dialog redraws to Change Wi-Fi. The state only, because
    // the installer drops a result it did not ask for (see improv.h).
    if (g_join.joined(up())) {
        plat::log("improv: on the network, telling the installer");
        sendState(improv::S_PROVISIONED);
    }

    if (g_trial) {
        if (joinedNew()) finishTrial();
        else if (plat::millis() - g_trialAt >= kTrialMs) {
            g_trial = false;
            plat::log("improv: \"%s\" did not answer, going back", g_newSsid);
            sendError(improv::E_CONNECT);
            switchTo(g_oldSsid, g_oldPass);
            sendState(improv::S_AUTHORIZED);
        }
    }
    if (g_scanning) {
        if (xEventGroupGetBits(s_wifi) & SCAN_DONE) finishScan();
        else if (plat::millis() - g_scanAt >= kScanMs) {
            esp_wifi_scan_stop();
            esp_wifi_clear_ap_list();
            sendError(improv::E_UNKNOWN);
            endScan();
        }
    }
}

} // namespace imp

// ---------------------------------------------------------------------------
// wifiWatch: the last network that worked (1.1.0). On each join, the network
// is kept as the one to go back to if it is not already; and a network that
// has not joined within a minute of boot is given up for the kept one. The
// rules and the file are core/recovery, where the host build can test them;
// this is the radio. Nothing is decided while Improv has the radio: its own
// trial already goes back to the old network when a new one fails.
// ---------------------------------------------------------------------------
static bool s_upSeen = false;

static void wifiWatch() {
    const bool up   = imp::up();
    const bool busy = imp::g_trial || imp::g_scanning || s_hold;
    if (!up) {
        s_upSeen = false;
    } else if (!s_upSeen && !busy) {
        s_upSeen = true;
        // On the network the radio was told to join, and no other: a join
        // that raced a switch must not be kept under the wrong name.
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
            !strcmp(reinterpret_cast<const char*>(ap.ssid), s_ssid))
            recovery::wifiJoined(s_ssid, s_pass);
    }
    char ssid[33], pass[65];
    if (recovery::wifiDue(plat::millis(), up, busy, s_ssid, ssid, pass)) imp::switchTo(ssid, pass);
}

// ---------------------------------------------------------------------------
// bbsTask: waits for the network, answering Improv meanwhile, then runs the
// scheduler forever
// ---------------------------------------------------------------------------
static void bbsTask(void*) {
    uint32_t told = plat::millis();
    while (!imp::up()) {
        imp::poll();
        wifiWatch();
        recovery::bootPoll(plat::millis());
        // Somebody may open the monitor long after boot, so the one line
        // that says what to do is repeated rather than scrolled away.
        if (!s_ssid[0] && plat::millis() - told >= 30000) {
            told = plat::millis();
            plat::log("wifi: no network set; waiting for Improv on this port");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    imp::poll();         // the join, said now rather than after the plugins start
    wifiWatch();
    recovery::bootPoll(plat::millis());
    netServicesStart();
    Bbs& bbs = Bbs::instance();
    while (!bbs.begin(s_port)) vTaskDelay(pdMS_TO_TICKS(1000));
    recovery::bootPoll(plat::millis());
    plugins::begin(bbs);

    // A second on the LED once the line is genuinely open. Wi-Fi being up is
    // not the same as the board being ready, and without a sign the only way
    // to find out is to dial in and be refused.
    plat::ledSignal(plat::millis(), 1000);

    // The task watchdog watches this loop from here on (1.1.0). The IDF
    // already watches both idle tasks, which catches a loop that spins
    // without yielding; subscribing the loop itself also catches one that
    // blocks for ever and lets the idle task run, which the idle check
    // cannot see. It is fed once a pass, so only a single pass longer than
    // CONFIG_ESP_TASK_WDT_TIMEOUT_S (30 s, sdkconfig.defaults) trips it, and
    // with CONFIG_ESP_TASK_WDT_PANIC that reboots the board, recorded in
    // reboots.log as "task watchdog". The longest legitimate passes are
    // seconds (a restore's biggest file, a DNS lookup when CONFIG restarts
    // the plugins). Everything before this line is left out on purpose:
    // waiting for Improv, the listener's retries and the plugins' start
    // (an SD card that will not answer) may take as long as they take.
    esp_err_t wd = esp_task_wdt_add(nullptr);
    if (wd != ESP_OK)
        ESP_LOGW(TAG, "task watchdog: could not watch the BBS loop (%s)", esp_err_to_name(wd));

#ifdef BBS_WDT_TEST
    // Bench build only (esp32dev_wdttest): stop the loop a minute after it
    // starts, blocked rather than spinning, so the idle task still runs and
    // only the loop's own subscription can notice. A board that restarts
    // with "task watchdog" in reboots.log proves the whole chain.
    const uint32_t wedgeAt = plat::millis() + 60000;
#endif

    for (;;) {
        bbs.tick();
        imp::poll();     // one non-blocking UART read when nothing is arriving
        wifiWatch();
        recovery::bootPoll(plat::millis());   // one comparison once the watch is over
        esp_task_wdt_reset();
#ifdef BBS_WDT_TEST
        if (static_cast<int32_t>(plat::millis() - wedgeAt) >= 0) {
            plat::log("wdt test: the BBS loop stops here; the watchdog should restart "
                      "the board within %d s", CONFIG_ESP_TASK_WDT_TIMEOUT_S);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
#endif
        vTaskDelay(1);   // let lower-priority tasks on this core breathe
    }
}

extern "C" void app_main(void) {
    // Improv reads the Wi-Fi bits from its first poll, which is before the
    // radio starts, so the group exists before anything else.
    s_wifi = xEventGroupCreate();

    // The console UART gets a driver so Improv can read it without blocking.
    // The log keeps writing the way it always has; only input changes hands,
    // and nothing else here ever read the console.
    // Without it every read would fail and the driver would log the failure
    // on every pass, so Improv is simply off.
    //
    // First thing, before NVS: from here a question the installer sends
    // while the board boots waits in the driver's buffer for the first poll.
    esp_err_t ue = uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0);
    imp::g_uartOk = ue == ESP_OK;
    if (!imp::g_uartOk) ESP_LOGE(TAG, "console UART driver: %s, Improv is off", esp_err_to_name(ue));

    // The BOOT-hold watch (1.1.0, core/recovery) starts looking as early as
    // anything, and is polled between the steps below and then from the BBS
    // task. BOOT held while RESET is let go is the ROM's download mode and
    // this code never runs, so the hold that counts is one that starts after
    // the firmware does: within recovery::kWindowMs of now.
    recovery::bootPoll(plat::millis());

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
    recovery::bootPoll(plat::millis());
    fsMount(BBS_USER_LABEL, BBS_USER_BASE);
    fsMount(BBS_LOGS_LABEL, BBS_LOGS_MOUNT);
    syscfg::load();          // hostname, TZ, NTP, staff passwords, limits, backup window
    s_port = syscfg::get().port;     // this boot's port, before anything says it
    // The activity LED, now rather than in Bbs::begin after the network: the
    // BOOT-hold watch shows its stages on it from the first seconds. Bbs::begin
    // asks for the same pin again later, and that changes nothing.
    plat::activityLedBegin(syscfg::get().ledGpio);
    recovery::bootPoll(plat::millis());
    // The first answer, as soon as there is a name to give (C_INFO reads
    // the settings). Everything before this line is quick on a board that
    // has booted before; the partition walks used to come first and now
    // come after it, with a poll between each slow step.
    imp::poll();
    // Take the free-space figures now, while there is nobody to stall. Each
    // one walks its whole partition; after this the board keeps them, and a
    // SYS or DASH reads the kept figure rather than paying the walk.
    {
        uint32_t t = 0, u = 0;
        plat::fsInfo(t, u);
        imp::poll();
        recovery::bootPoll(plat::millis());
        plat::userInfo(t, u);
    }
    imp::poll();
    recovery::bootPoll(plat::millis());
    wifiStart();
    imp::g_radio = true;
    imp::poll();
    recovery::bootPoll(plat::millis());

#if CONFIG_FREERTOS_UNICORE
    const BaseType_t core = 0;
#else
    const BaseType_t core = BBS_TASK_CORE;
#endif
    xTaskCreatePinnedToCore(bbsTask, "bbs", BBS_TASK_STACK, nullptr,
                            BBS_TASK_PRIO, nullptr, core);
}
