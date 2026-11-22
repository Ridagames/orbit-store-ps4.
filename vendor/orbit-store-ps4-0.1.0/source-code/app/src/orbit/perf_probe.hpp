// Orbit Store TV app - Test-only timing of the parts of a frame.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Only in ORBIT_PERF_TRACE builds (UI speed checks on the console); in every
// other build ORBIT_PERF_SCOPE does nothing. Each scope adds its time to one
// slot; main.cpp logs the slots of every frame slower than 50 ms.

#pragma once

#ifdef ORBIT_PERF_TRACE
#include <cstdint>

namespace orbit::perf
{

enum Slot : int
{
    store_tick,
    art_tick,
    art_upload,
    screen_update,
    screen_draw,
    render,
    swap,
    browse_filter,
    browse_counts,
    input,
    audio,
    idle,
    log_write,
    kSlots
};

inline const char *const kSlotNames[kSlots] = {"store", "art", "upload", "update", "draw", "render", "swap", "filter", "counts", "input", "audio", "idle", "log"};
inline std::int64_t slot_us[kSlots] = {};

std::int64_t now_us();

struct Scope
{
    explicit Scope(Slot s) : slot(s), start(now_us()) {}
    ~Scope() { slot_us[slot] += now_us() - start; }
    Slot slot;
    std::int64_t start;
};

} // namespace orbit::perf

#define ORBIT_PERF_JOIN2(a, b) a##b
#define ORBIT_PERF_JOIN(a, b) ORBIT_PERF_JOIN2(a, b)
#define ORBIT_PERF_SCOPE(slot) ::orbit::perf::Scope ORBIT_PERF_JOIN(perf_scope_, __LINE__)(::orbit::perf::slot)
#else
#define ORBIT_PERF_SCOPE(slot) ((void)0)
#endif
