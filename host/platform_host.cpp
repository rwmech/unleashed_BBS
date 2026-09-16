/*
 * File:        host/platform_host.cpp
 * Description: Linux host implementation of the platform layer, so the
 *              BBS core can be run and tested on a PC before flashing.
 * Listing:     COMPLETE FILE
 * Libraries:   libc
 */
#include "platform/platform.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace {
std::string g_fsBase = "../data";
}

// host-only: set by main_host.cpp
void hostSetFsBase(const char* path) { g_fsBase = path; }

namespace plat {

uint32_t millis() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

uint32_t random32() {
    return static_cast<uint32_t>(rand());
}

const char* fsBase() {
    return g_fsBase.c_str();
}

HeapStats heap() {
    return HeapStats{ 0, 0, 0, false };
}

void log(const char* fmt, ...) {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stdout, "[%6lu.%03lu] ", static_cast<unsigned long>(ts.tv_sec),
            static_cast<unsigned long>(ts.tv_nsec / 1000000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

} // namespace plat
