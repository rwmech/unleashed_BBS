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
 * License:      GNU General Public License v3 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>. The full
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
// The rule lives in core/netfallback.h, shared with core/recovery, so the
// factory reset's console line and the network dialled here cannot disagree.
#include "core/netfallback.h"

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
#include "esp_task_wdt.h"
#include "mdns.h"
#include "sdkconfig.h"
#ifdef BBS_HAS_ETH
#include "esp_eth.h"                     // ETH_EVENT, for the wire's events (netPick)
#endif
#include <cstring>
#include <ctime>
#ifdef BBS_HAS_ETH
#include <atomic>
#endif

static const char* TAG = "main";
static EventGroupHandle_t s_wifi = nullptr;
static const EventBits_t WIFI_UP   = BIT0;
static const EventBits_t SCAN_DONE = BIT1;
#ifdef BBS_HAS_ETH
// Ethernet first (1.1.2, a board with a wired port). ETH_UP is the wire with
// an address; NET_UP is either, and is what "the board is online" means to
// the BBS task and to Improv. WIFI_UP stays the radio's alone, for Improv's
// trial and the last good network, which are about Wi-Fi and nothing else.
static const EventBits_t ETH_UP    = BIT2;
static const EventBits_t NET_UP    = WIFI_UP | ETH_UP;
// s_ethHold: Wi-Fi is standing by for Ethernet, so nothing dials it. Set
// while the wire is up or still has its first seconds to come up.
static volatile bool     s_ethHold = false;
static bool              s_ethOn   = false;     // ethernet = yes and the chip answered
// s_beside: Wi-Fi joins beside the wire (1.2.1, wifi_with_ethernet), read at
// boot. Only once a network is set does it mean anything (besideNow), so a
// board provisioned by Improv while on the wire keeps the Wi-Fi it joined.
static bool              s_beside  = false;
namespace imp { extern bool g_trial; }
static bool ethTrial() { return imp::g_trial; }
#else
static const EventBits_t NET_UP    = WIFI_UP;
#endif

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
#ifdef BBS_HAS_ETH
static bool besideNow() { return s_beside && s_ssid[0]; }
// The redial beside the wire (1.2.1-eth.2, eth.3). A network that will not
// answer failed every 2-3 s for good, with a console line and a supplicant
// round each time. While the wire has an address the callers are on it, but
// the link is not: it drops DISCOVER until the station is associated, so
// every second Wi-Fi is down is a second no sat can reach the board. So the
// wait follows the reason (redialWait):
//   a reason that cannot fix itself (a wrong password, a handshake that
//     times out, a security the AP does not offer): 30 s, doubling to 5
//     minutes, one console line a step;
//   the AP gone (not found, beacons lost, the AP leaving, a router
//     rebooting): a steady 15 s, said once, so the station is back within
//     15 s of the AP.
// The first drop after a join redials at once whatever the reason. A join
// or an association resets it (a reconnect that keeps its lease raises
// STA_CONNECTED and never GOT_IP); the wire going dials at once.
// s_redialAt is stamped on the event loop's task and taken by ethWatch on
// the loop with a compare-and-swap, so a stamp written between its read and
// its clear is not lost.
constexpr uint32_t kRedialFirstMs  = 30000;
constexpr uint32_t kRedialMaxMs    = 300000;
constexpr uint32_t kRedialSteadyMs = 15000;
static std::atomic<uint32_t> s_redialAt{0};   // millis to dial again, 0 none waiting
static std::atomic<uint32_t> s_redialGap{0};  // the next stubborn wait, 0 the first drop since a join
static std::atomic<bool>     s_redialSteady{false};   // the last wait was the steady one (said once)

// redialStubborn: a disconnect reason another try in 15 s will not change.
static bool redialStubborn(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_GROUP_CIPHER_INVALID:
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
        case WIFI_REASON_AKMP_INVALID:
        case WIFI_REASON_CIPHER_SUITE_REJECTED:
        case WIFI_REASON_BAD_CIPHER_OR_AKM:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return true;
        default:
            return false;                 // the AP gone, or a passing fault
    }
}

// redialReset: a join or an association. The next drop redials at once.
static void redialReset() {
    s_redialGap.store(0);
    s_redialAt.store(0);
    s_redialSteady.store(false);
}
#endif

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
#ifdef BBS_HAS_ETH
        if (s_ethHold) return;                  // the wire goes first (ethWatch)
#endif
        if (s_ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(s_wifi, SCAN_DONE);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        // Associated, which is the earliest point the setting can be made to
        // stick. A reconnect that keeps the same lease never reaches the
        // GOT_IP branch below, and that is the hole the lag came back
        // through.
        noSleep();
#ifdef BBS_HAS_ETH
        // The same hole for the redial's backoff (1.2.1-eth.3): reset here,
        // or a gap ratchets towards 5 minutes over days of AP blips.
        redialReset();
#endif
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* e = static_cast<wifi_event_sta_disconnected_t*>(data);
        xEventGroupClearBits(s_wifi, WIFI_UP);
        if (s_hold || !s_ssid[0]) return;       // Improv has the radio
#ifdef BBS_HAS_ETH
        // Stood down for the wire, on purpose; except while Improv tries a
        // network, which needs every try of its minute.
        if (s_ethHold && !ethTrial()) return;
#endif
#ifdef BBS_HAS_ETH
        // Beside the wire with the wire up: back off after the first try.
        // Not during Improv's trial, which needs every try of its minute.
        if (besideNow() && plat::ethInfo().up && !ethTrial()) {
            const uint32_t gap = s_redialGap.load();
            if (gap) {
                const bool stubborn = redialStubborn(e->reason);
                const uint32_t wait = stubborn ? gap : kRedialSteadyMs;
                uint32_t at = plat::millis() + wait;
                if (!at) at = 1;
                s_redialAt.store(at);
                if (stubborn) {
                    s_redialGap.store(gap * 2 > kRedialMaxMs ? kRedialMaxMs : gap * 2);
                    s_redialSteady.store(false);
                    ESP_LOGW(TAG, "wifi down (reason %u); on the wire, next try in %u s",
                             static_cast<unsigned>(e->reason), static_cast<unsigned>(wait / 1000u));
                } else if (!s_redialSteady.exchange(true)) {
                    ESP_LOGW(TAG, "wifi down (reason %u, the AP gone?); on the wire, trying every %u s",
                             static_cast<unsigned>(e->reason), static_cast<unsigned>(wait / 1000u));
                }
                return;
            }
            s_redialGap.store(kRedialFirstMs);     // this one at once, the next waits
        }
#endif
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
#ifdef BBS_HAS_ETH
        redialReset();                          // joined: the next drop redials at once
#endif
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
#ifdef BBS_HAS_ETH
    // Ethernet first (1.1.2). Started before the radio, and Wi-Fi held from
    // its first moment, so the station does not dial at STA_START only to be
    // stood down a second later. The radio still starts: Improv scans with
    // it, it is the fallback, and the 1.2.0 link needs it running.
    // ethernet = no with no Wi-Fi network to use instead would come up on
    // neither, and only a cable and Improv could fix it: the wire is used
    // anyway, and the console says why.
    if (sc.ethernet || !s_ssid[0]) {
        if (!sc.ethernet) ESP_LOGW(TAG, "eth: ethernet = no, but no Wi-Fi network is set; using the wire");
        s_ethOn = plat::ethBegin(sc.hostname);
        // Wi-Fi beside the wire (1.2.1): the station dials from STA_START as
        // on every other board, and the wire only takes the default route
        // (netPick). ethernet = no that came up on the wire for want of a
        // network wanted Wi-Fi alone, so a network Improv gives it later is
        // joined too.
        s_beside  = s_ethOn && (sc.wifiWithEth || !sc.ethernet);
        s_ethHold = s_ethOn && !besideNow();
        if (besideNow()) ESP_LOGI(TAG, "eth: Wi-Fi joins beside the wire; callers on the wire");
    } else {
        ESP_LOGI(TAG, "eth: off (ethernet = no), Wi-Fi only");
    }
#endif
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

#ifdef BBS_HAS_ETH
// ---------------------------------------------------------------------------
// netPick: which interface the board speaks from, when both are up (1.2.1).
// Run on the default event loop's task, never on the BBS loop: registered
// in netServicesStart for the five events that change the answer, and once
// there to take over from the automatic choice.
//
//   default route  the wire while it has an address, else the station while
//                  it has one. esp_netif's own choice is by route_prio among
//                  the interfaces that are UP, and the wire is up with a
//                  link before DHCP answers: a cable into a dead switch kept
//                  the default on an interface with no address, and lwIP
//                  then has no route at all (ip4_route), so NTP, DNS and
//                  announce failed while Wi-Fi was joined. Set by hand here,
//                  only on a change. Accepted calls are unaffected either
//                  way: IDF's source-routing hook (LWIP_HOOK_IP4_ROUTE_SRC)
//                  answers each from the interface its address belongs to.
//   mDNS           the wire's address alone while the wire has one, so
//                  <hostname>.local sends callers to the wire. The mdns
//                  component answers on both interfaces of one subnet with
//                  both addresses (its "duplicate" interfaces); the
//                  station's answers are switched off while the wire is up
//                  and on again when the wire goes. mDNS's own handlers are
//                  base-level (ESP_EVENT_ANY_ID) and run before these, which
//                  are registered per event id: so the station's GOT_IP,
//                  which re-enables it there, is answered here after.
// The state is read from what the earlier handlers of the same event left:
// WIFI_UP (onNet, base-level for WIFI_EVENT, per id for STA_GOT_IP, both
// registered before these) and plat::ethInfo (onEthEvent, likewise).
// ---------------------------------------------------------------------------
static esp_netif_t* s_nifSta   = nullptr;
static esp_netif_t* s_nifEth   = nullptr;
static uint8_t      s_route    = 0;          // 0 automatic, 1 the wire, 2 the station
static bool         s_mdnsQuiet = false;     // the station's mDNS answers are off
static bool         s_mdnsEthOff = false;    // the wire's are, for an address it lost

static void netPick(bool staNew) {
    if (!s_nifSta) s_nifSta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!s_nifEth) s_nifEth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    const bool eth = plat::ethInfo().up;
    const bool sta = xEventGroupGetBits(s_wifi) & WIFI_UP;
    const uint8_t want = eth ? 1 : sta ? 2 : 0;
    // Nothing up: leave the last choice, which the next GOT_IP replaces.
    if (want && want != s_route) {
        esp_netif_t* nif = want == 1 ? s_nifEth : s_nifSta;
        if (nif && esp_netif_set_default_netif(nif) == ESP_OK) {
            s_route = want;
            plat::log("net: the board speaks from %s", want == 1 ? "Ethernet" : "Wi-Fi");
        }
    }
    // The wire with a link and no address (IP_EVENT_ETH_LOST_IP with the
    // cable in): mDNS's own handlers turn the wire's answers off only at a
    // link down, so it went on answering with 0.0.0.0. Off here until the
    // next ETH GOT_IP, where mDNS turns them on again itself.
    if (s_nifEth) {
        if (!eth && !s_mdnsEthOff) {
            mdns_netif_action(s_nifEth, MDNS_EVENT_DISABLE_IP4);
            s_mdnsEthOff = true;
        } else if (eth) {
            s_mdnsEthOff = false;
        }
    }
    if (!s_nifSta) return;
    if (eth && sta) {
        // A station GOT_IP had mDNS turn it on again in this same event.
        if (!s_mdnsQuiet || staNew) {
            mdns_netif_action(s_nifSta, MDNS_EVENT_DISABLE_IP4);
            s_mdnsQuiet = true;
        }
    } else if (sta && s_mdnsQuiet) {
        // The wire went: the station answers again, and says so at once.
        mdns_netif_action(s_nifSta, static_cast<mdns_event_actions_t>(MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4));
        s_mdnsQuiet = false;
    } else if (!sta) {
        s_mdnsQuiet = false;     // mDNS turned it off at the disconnect, and turns it on at the next GOT_IP
    }
}

// The first pick, posted by netServicesStart (the station may already be up,
// and mdns_init has just enabled it).
ESP_EVENT_DEFINE_BASE(BBS_NET_EVENT);

static void onPick(void*, esp_event_base_t base, int32_t id, void*) {
    netPick((base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) || base == BBS_NET_EVENT);
}
#endif

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
    } else {
        mdns_hostname_set(cfg.hostname);
        mdns_instance_name_set(BBS_NAME);
        mdns_service_add(BBS_NAME, "_telnet", "_tcp", s_port, nullptr, 0);
        ESP_LOGI(TAG, "mdns: %s.local, _telnet._tcp port %u", cfg.hostname, static_cast<unsigned>(s_port));
    }
#ifdef BBS_HAS_ETH
    // After mdns_init, so these run after its handlers (netPick). Per event
    // id, which also puts them after onNet's and onEthEvent's. A failed
    // mdns_init leaves the route to manage; its calls then just refuse.
    if (s_ethOn) {
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onPick, nullptr);
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onPick, nullptr);
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &onPick, nullptr);
        esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &onPick, nullptr);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &onPick, nullptr);
        esp_event_handler_register(BBS_NET_EVENT, 0, &onPick, nullptr);
        // mdns_init turned on every interface that had an address. The first
        // pick is posted to the event loop rather than made here, so netPick
        // only ever runs on one task and its statics need no lock. A full
        // queue refuses it, and then nothing is picked here (a pick on this
        // task could race the event task's and leave s_mdnsQuiet wrong): the
        // next GOT_IP of either interface picks, and until then esp_netif's
        // own choice stands.
        if (esp_event_post(BBS_NET_EVENT, 0, nullptr, 0, pdMS_TO_TICKS(100)) != ESP_OK)
            ESP_LOGW(TAG, "net: the first pick could not be queued; the next address event makes it");
    }
#endif
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

#ifdef BBS_CHIP_S3
constexpr char kImprovChip[] = "ESP32-S3";
#else
constexpr char kImprovChip[] = "ESP32";
#endif
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

bool up() { return xEventGroupGetBits(s_wifi) & NET_UP; }

// send: one frame, with a newline in front of it. The browser only looks
// for a packet at the start of a line, and the console may be part way
// through one. stdout's lock is held so a log line from another task cannot
// land in the middle of a packet: ESP_LOG takes the same lock, and a packet
// with a log line spliced into it fails its checksum and is never seen.
// plat::consoleWrite rather than stdout itself, because stdout turns every
// 0x0A into CR LF and a length or checksum byte can be 0x0A. That is UART0
// on the ESP32 and the chip's own USB on the S3 (1.1.0), whichever port the
// browser is holding.
// The IDF's newlib has no flockfile(), and its _flockfile macro does not
// compile as C++. __lock_acquire_recursive on the stream's own lock is what
// that macro expands to for a stream that is not a string, so this is the
// lock vfprintf takes and not a copy of it.
void send(const uint8_t* b, size_t n) {
    if (!n) return;
    __lock_acquire_recursive(stdout->_lock);
    fflush(stdout);
    static const uint8_t kNl = '\n';
    plat::consoleWrite(&kNl, 1);
    plat::consoleWrite(b, n);
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
#ifdef BBS_HAS_ETH
    // The address in use: the wire's while it has one.
    esp_ip4_addr_t ip = s_ip;
    const plat::EthInfo e = plat::ethInfo();
    if (e.up) ip.addr = e.ip;
    snprintf(url, sizeof(url), "telnet://" IPSTR ":%u", IP2STR(&ip), static_cast<unsigned>(s_port));
#else
    snprintf(url, sizeof(url), "telnet://" IPSTR ":%u", IP2STR(&s_ip), static_cast<unsigned>(s_port));
#endif
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
#ifdef BBS_HAS_ETH
    // Wi-Fi's own state, not the wire's: a station dialling while the wire
    // carries the callers would otherwise hold the scan refused, and the
    // spin below would run with those callers on.
    if (!(xEventGroupGetBits(s_wifi) & WIFI_UP)) esp_wifi_disconnect();
#else
    if (!up()) esp_wifi_disconnect();
#endif
    xEventGroupClearBits(s_wifi, SCAN_DONE);
    esp_err_t e = ESP_FAIL;
    for (int i = 0; i < 100; ++i) {
        e = esp_wifi_scan_start(nullptr, false);
        if (e != ESP_ERR_WIFI_STATE) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (e != ESP_OK) {
        s_hold = false;
#ifdef BBS_HAS_ETH
        if (!(xEventGroupGetBits(s_wifi) & WIFI_UP) && s_ssid[0] && !s_ethHold) esp_wifi_connect();
#else
        if (!up() && s_ssid[0]) esp_wifi_connect();
#endif
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
#ifdef BBS_HAS_ETH
    if (!(xEventGroupGetBits(s_wifi) & WIFI_UP) && s_ssid[0] && !s_ethHold) esp_wifi_connect();
#else
    if (!up() && s_ssid[0]) esp_wifi_connect();
#endif
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
        // The chip family, as the installer names it: "ESP32" or "ESP32-S3".
        // The firmware's name as a person reads it, micro sign and all, in
        // UTF-8 (1.1.1): the installer decodes it and matches either
        // spelling from site 1.3.8.
        const char* s[] = { BBS_NAME, BBS_VERSION_SHOWN, kImprovChip,
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
#ifdef BBS_HAS_ETH
    if (!(xEventGroupGetBits(s_wifi) & WIFI_UP)) return false;   // the radio's own, never the wire's
#else
    if (!up()) return false;
#endif
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    return !strcmp(reinterpret_cast<const char*>(ap.ssid), g_newSsid);
}

// poll: whatever has arrived, then the join, the trial and the scan.
void poll() {
    if (!g_uartOk) return;                  // the driver refused; say so once, at boot
    uint8_t buf[64];
    size_t n = plat::consoleRead(buf, sizeof(buf));
    for (size_t i = 0; i < n; ++i) {
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

#ifdef BBS_HAS_ETH
// The fallback's switch, a pass at a time (wifiWatch). s_fbAt is when it
// began, 0 none. s_hold is held throughout, so the disconnect handler
// does not redial the network being replaced.
static char     s_fbSsid[33];
static char     s_fbPass[65];
static uint32_t s_fbAt = 0;

static void fbBegin() {
    s_hold = true;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi, WIFI_UP);
    s_fbAt = plat::millis();
    if (!s_fbAt) s_fbAt = 1;
}

// fbStep: one try of the switch; false when none is under way.
static bool fbStep() {
    if (!s_fbAt) return false;
    const esp_err_t e = wifiUse(s_fbSsid, s_fbPass);
    if (e == ESP_ERR_WIFI_STATE && plat::since(plat::millis(), s_fbAt) < 5000) return true;   // next pass
    s_fbAt = 0;
    s_hold = false;
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "wifi: the radio refused \"%s\" (%s)", s_fbSsid, esp_err_to_name(e));
        return false;
    }
    if (s_ssid[0]) esp_wifi_connect();
    return false;
}
#endif

static void wifiWatch() {
#ifdef BBS_HAS_ETH
    const bool up   = xEventGroupGetBits(s_wifi) & WIFI_UP;       // Wi-Fi's, not the wire's
    // A join is kept as the last good network whatever the wire is doing:
    // Improv's trial joins while the wire is up, and ethWatch stands Wi-Fi
    // down only after this has run in the same pass. Standing by for the
    // wire is not failing to join, though, so the fallback judges nothing
    // while the hold is on (and ethWatch re-times it when the hold goes).
    const bool busyJoin = imp::g_trial || imp::g_scanning || s_hold;
    // Nor while Wi-Fi is beside a wire that has an address (1.2.1-eth.2):
    // a board whose callers are on the wire gains nothing from going back
    // to the last good network. Read from the loop's own ETH_UP bit, not
    // plat::ethInfo (eth.3): the event task clears that first, and in the
    // pass the wire dropped this runs before ethWatch, which clears ETH_UP
    // and gives the trial its minute afresh (recovery::wifiBegin) in one
    // step. Read from ethInfo, a timer armed at boot fired in that pass.
    const bool wired    = besideNow() && (xEventGroupGetBits(s_wifi) & ETH_UP);
    const bool busy     = busyJoin || s_ethHold || wired || s_fbAt;
#else
    const bool up   = imp::up();
    const bool busy = imp::g_trial || imp::g_scanning || s_hold;
    const bool busyJoin = busy;
#endif
    if (!up) {
        s_upSeen = false;
    } else if (!s_upSeen && !busyJoin) {
        s_upSeen = true;
        // On the network the radio was told to join, and no other: a join
        // that raced a switch must not be kept under the wrong name.
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
            !strcmp(reinterpret_cast<const char*>(ap.ssid), s_ssid))
            recovery::wifiJoined(s_ssid, s_pass);
    }
#ifdef BBS_HAS_ETH
    // Going back without imp::switchTo's spin (1.2.1-eth.3): switchTo tries
    // esp_wifi_set_config up to 100 times 50 ms apart while the station is
    // still part way through a connect, up to 5 s with the loop stopped.
    // Here the radio is told to drop the connect, and the set_config is
    // tried once a pass until it takes or 5 s go by (fbStep).
    if (fbStep()) return;
    if (recovery::wifiDue(plat::millis(), up, busy, s_ssid, s_fbSsid, s_fbPass)) fbBegin();
#else
    char ssid[33], pass[65];
    if (recovery::wifiDue(plat::millis(), up, busy, s_ssid, ssid, pass)) imp::switchTo(ssid, pass);
#endif
}

#ifdef BBS_HAS_ETH
// ---------------------------------------------------------------------------
// ethWatch: Ethernet first, Wi-Fi as the fallback (1.1.2). Polled with
// wifiWatch, from the wait for the network and from every pass: reads of
// what the Ethernet events left (plat::ethInfo), and at most one call into
// the radio when the interface changes, which returns at once.
//
//   the wire has an address   ETH_UP; Wi-Fi stands down (held, and left
//                             if it had joined), unless Improv is using it
//   no address for kBootMs    from boot, or kLostMs after the wire had one:
//                             the hold goes and Wi-Fi dials as it always has
//   the wire back             Wi-Fi stands down again
//
// That table is wifi_with_ethernet = no. As shipped (1.2.1) Wi-Fi joins
// beside the wire and is never stood down: the hold is off from boot, and
// the wire's part is the default route and mDNS (netPick). Callers reach
// the board on either address; the wire is the one it gives out.
//
// The listeners are bound to every interface (INADDR_ANY), so neither moves:
// a caller on the interface that went away is dropped by TCP (keepalive, or
// a reset once Wi-Fi leaves), and a new call arrives on the other. The
// directory hears the address in use on the next heartbeat.
// ---------------------------------------------------------------------------
static void ethWatch() {
    if (!s_ethOn) return;
    constexpr uint32_t kBootMs = 10000;  // DHCP on a home router answers in 2-3 s
    // After a loss: a cable reseated inside it keeps Wi-Fi out of it, though
    // the wire's own callers are already gone (the netif going down resets
    // its address, and lwIP aborts every connection on it).
    constexpr uint32_t kLostMs = 3000;
    static bool     wasUp = false;       // the wire has been up this boot
    static uint32_t since = plat::millis();   // without an address since
    const uint32_t now  = plat::millis();
    const bool     busy = imp::g_trial || imp::g_scanning || s_hold;
    // Wi-Fi beside the wire (1.2.1): never stood down. The wire only takes
    // the default route and mDNS (netPick, on the event loop), so a cable
    // in or out moves neither the station nor a caller on it.
    const bool     beside = besideNow();
    const plat::EthInfo e = plat::ethInfo();
    if (e.up) {
        since = now;
        if (!(xEventGroupGetBits(s_wifi) & ETH_UP)) {
            xEventGroupSetBits(s_wifi, ETH_UP);
            esp_ip4_addr_t a;
            a.addr = e.ip;
            ESP_LOGI(TAG, "online on Ethernet " IPSTR ", %u Mb/s  dial in: telnet " IPSTR " %u",
                     IP2STR(&a), static_cast<unsigned>(e.mbps), IP2STR(&a), static_cast<unsigned>(s_port));
            if (!beside && !s_ethHold && s_ssid[0]) ESP_LOGI(TAG, "eth: back; Wi-Fi stands down");
        }
        wasUp = true;
        if (beside) {
            // Held only when the board had no network at boot and Improv
            // has given it one since: its trial joined, and it stays.
            if (s_ethHold) {
                s_ethHold = false;
                if (!busy && !(xEventGroupGetBits(s_wifi) & WIFI_UP)) esp_wifi_connect();
            }
            // A backed-off redial that has come due (onNet). One call into
            // the radio, which returns at once.
            uint32_t at = s_redialAt.load();
            if (at && static_cast<int32_t>(now - at) >= 0 && s_redialAt.compare_exchange_strong(at, 0)) {
                if (!busy && !(xEventGroupGetBits(s_wifi) & WIFI_UP)) esp_wifi_connect();
            }
            return;
        }
        s_ethHold = true;
        // Joined: leave, unless Improv has the radio (its trial decides its
        // own network; this runs again once it is done). Asked once a
        // second at most, while the disconnect event is on its way.
        static uint32_t askedAt = 0;
        if (!busy && (xEventGroupGetBits(s_wifi) & WIFI_UP) && plat::since(now, askedAt) >= 1000) {
            askedAt = now;
            esp_wifi_disconnect();
        }
        return;
    }
    if (xEventGroupGetBits(s_wifi) & ETH_UP) {
        xEventGroupClearBits(s_wifi, ETH_UP);
        since = now;
        ESP_LOGW(TAG, "eth: no address (link %s)%s", e.link ? "up" : "down",
                 beside ? "; Wi-Fi carries on" : "");
        if (beside) {
            // Wi-Fi is the board's only way out now: a redial that was
            // backing off goes at once, and a network still on its trial
            // gets its minute from here (wifiWatch judged nothing while the
            // wire carried the board).
            s_redialGap.store(0);
            s_redialSteady.store(false);
            if (s_redialAt.exchange(0)) {
                if (!busy && !(xEventGroupGetBits(s_wifi) & WIFI_UP)) esp_wifi_connect();
            }
            recovery::wifiBegin(now, s_ssid, s_pass);
        }
    }
    if (s_ethHold && plat::since(now, since) >= (wasUp ? kLostMs : kBootMs)) {
        s_ethHold = false;
        if (!s_ssid[0]) {
            ESP_LOGW(TAG, "eth: no address, and no Wi-Fi network set to fall back on");
        } else {
            ESP_LOGW(TAG, "eth: no address in %u s; Wi-Fi takes over (\"%s\")",
                     static_cast<unsigned>((wasUp ? kLostMs : kBootMs) / 1000), s_ssid);
            // The last good network's minute starts now, not at boot: a
            // network set while the wire carried the board has never been
            // dialled, and must get its whole trial before the board goes
            // back to the old one.
            recovery::wifiBegin(now, s_ssid, s_pass);
            if (!busy) esp_wifi_connect();
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// bbsTask: waits for the network, answering Improv meanwhile, then runs the
// scheduler forever
// ---------------------------------------------------------------------------
static void bbsTask(void*) {
    uint32_t told = plat::millis();
    while (!imp::up()) {
        imp::poll();
        wifiWatch();
#ifdef BBS_HAS_ETH
        ethWatch();
#endif
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
#ifdef BBS_HAS_ETH
    ethWatch();
#endif
    recovery::bootPoll(plat::millis());
    netServicesStart();
    Bbs& bbs = Bbs::instance();
    while (!bbs.begin(s_port)) vTaskDelay(pdMS_TO_TICKS(1000));
#if BBS_HAS_SSH
    // SSH's own port (1.1.2), advertised once it is really listening, so an
    // SSH client that browses for _ssh._tcp never finds a dead one.
    if (bbs.sshPort()) {
        mdns_service_add(BBS_NAME, "_ssh", "_tcp", bbs.sshPort(), nullptr, 0);
        ESP_LOGI(TAG, "mdns: _ssh._tcp port %u", static_cast<unsigned>(bbs.sshPort()));
    }
#endif
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
#ifdef BBS_HAS_ETH
        ethWatch();      // plain reads; the radio only when the interface changes
#endif
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

    // The console gets a driver so Improv can read it without blocking:
    // UART0 on the ESP32, the chip's own USB on the S3 (plat::consoleBegin).
    // Nothing else here ever read the console.
    // Without it every read would fail and the driver would log the failure
    // on every pass, so Improv is simply off.
    //
    // First thing, before NVS: from here a question the installer sends
    // while the board boots waits in the driver's buffer for the first poll.
    imp::g_uartOk = plat::consoleBegin();
    if (!imp::g_uartOk) ESP_LOGE(TAG, "console driver would not install, Improv is off");

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
              BBS_NAME, BBS_VERSION_SHOWN, static_cast<unsigned>(h.freeBytes),
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
    // The free-space figures are measured on the background runner once the
    // BBS starts (space::refresh in Bbs::begin, 1.1.2), not walked here.
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
