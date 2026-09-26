// ===========================================================================
//  µnleashed BBS
//  Electronic freedom on a microcontroller.
// ===========================================================================
//
// File:         src/core/space.cpp
// Module:       Core / how full the storage is (1.1.2)
//
// Purpose:      See space.h. One runner job measures whichever figures were
//               asked for into its own copy; the loop publishes the copy
//               once the job says DONE, so a figure is never half written
//               where a screen can read it (the figures are two 64-bit
//               words, which no single store covers).
//
// Targets:      ESP32-WROOM-32E (ESP-IDF 5.3.1) and the Linux host build
// See also:     src/core/space.h
//
// Copyright 2026 - Robert Mech
// License:      GNU General Public License v3 or later
// SPDX-License-Identifier: GPL-3.0-or-later
// ===========================================================================

#include "space.h"
#include "runner.h"
#include "clock.h"

#include <cstdio>

namespace space {
namespace {

Fig     g_fig[plat::PART_COUNT];
uint8_t g_stale = (1u << plat::PART_COUNT) - 1;   // everything, until the first measure
uint8_t g_want  = 0;                              // asked for, not yet posted

struct Measure {
    runner::Job job;
    uint8_t     mask = 0;                         // in: which to measure
    Fig         out[plat::PART_COUNT];            // out: the runner's
};
Measure g_m;

void work(runner::Job&) {
    for (uint8_t p = 0; p < plat::PART_COUNT; ++p) {
        if (!(g_m.mask & (1u << p))) continue;
        Fig f;
        f.valid = plat::measure(static_cast<plat::Part>(p), f.total, f.used);
        f.atMs  = plat::millis();
        if (!f.atMs) f.atMs = 1;
        g_m.out[p] = f;
        runner::breathe();
    }
}

}  // namespace

Fig get(plat::Part p) {
    return p < plat::PART_COUNT ? g_fig[p] : Fig();
}

uint64_t freeBytes(plat::Part p) {
    Fig f = get(p);
    return f.valid && f.total > f.used ? f.total - f.used : 0;
}

void stale(plat::Part p) {
    if (p < plat::PART_COUNT) g_stale = static_cast<uint8_t>(g_stale | (1u << p));
}

void forget(plat::Part p) {
    if (p >= plat::PART_COUNT) return;
    g_fig[p] = Fig();
    stale(p);
}

// post: the job for whatever is wanted, when none is out. Loop only.
bool post() {
    if (!g_want || !runner::idle(g_m.job)) return false;
    g_m.mask = g_want;
    for (Fig& f : g_m.out) f = Fig();
    g_m.job.work = work;
    g_m.job.name = "space";
    // Cleared as it is posted: a write that lands while the runner measures
    // marks its partition again, and the next refresh takes it again.
    g_stale = static_cast<uint8_t>(g_stale & ~g_want);
    const uint8_t mask = g_want;
    g_want = 0;
    if (runner::post(g_m.job)) return true;
    g_stale = static_cast<uint8_t>(g_stale | mask);
    return false;
}

bool refresh(bool all) {
    const uint8_t mask = all ? static_cast<uint8_t>((1u << plat::PART_COUNT) - 1) : g_stale;
    // Wanted, then posted when the runner is free of the last one: a staff
    // login during the boot's own measure, or a FORCE during a login's, is
    // measured straight after rather than refused.
    g_want = static_cast<uint8_t>(g_want | mask);
    if (!g_want) return true;
    if (!runner::idle(g_m.job)) return true;
    return post() || !g_want;
}

bool busy() {
    return g_want || !runner::idle(g_m.job);
}

void tick() {
    if (runner::done(g_m.job)) {
        for (uint8_t p = 0; p < plat::PART_COUNT; ++p)
            if (g_m.mask & (1u << p)) g_fig[p] = g_m.out[p];
        runner::collect(g_m.job);
    }
    if (g_want && runner::idle(g_m.job)) post();
}

void asOf(char* out, size_t n) {
    if (!n) return;
    out[0] = '\0';
    uint32_t oldest = 0;
    for (const Fig& f : g_fig)
        if (f.valid && (!oldest || static_cast<int32_t>(f.atMs - oldest) < 0)) oldest = f.atMs;
    if (!oldest) return;
    const uint32_t ago = (plat::millis() - oldest) / 1000u;
    if (clk::valid()) {
        clk::fmtEpoch(out, n, "%H:%M", clk::epoch() - ago);
        return;
    }
    if (ago < 60) snprintf(out, n, "%u s ago", static_cast<unsigned>(ago));
    else          snprintf(out, n, "%u min ago", static_cast<unsigned>(ago / 60u));
}

}  // namespace space
