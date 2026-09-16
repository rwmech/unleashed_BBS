/*
 * File:        host/main_host.cpp
 * Description: Linux host entry point. Runs the same BBS core as the
 *              ESP32 so screens, detection and effects can be tried with
 *              SyncTERM, a terminal emulator, or VICE + a TCP modem bridge.
 *              Usage: ./bbs_host [data_dir] [port]
 * Listing:     COMPLETE FILE
 * Libraries:   libc
 */
#include "core/bbs.h"
#include "core/sysconfig.h"
#include "platform/platform.h"
#include <csignal>
#include <cstdlib>
#include <ctime>

void hostSetFsBase(const char* path);

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    srand(static_cast<unsigned>(time(nullptr)));
    hostSetFsBase(argc > 1 ? argv[1] : "../data");
    uint16_t port = static_cast<uint16_t>(argc > 2 ? atoi(argv[2]) : BBS_PORT);
    syscfg::load();          // the host clock is already set, no NTP here

    Bbs& bbs = Bbs::instance();
    if (!bbs.begin(port)) return 1;
    for (;;) bbs.tick();
}
