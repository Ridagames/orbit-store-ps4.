// ps4-homebrew-ui - PS4 system services: logging, clock, splash screen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace hui::sys
{

// VSH-authorized Piglet apps explicitly load their required system services.
bool initialize_platform();
void initialize_logging();

// Monotonic time in microseconds.
std::int64_t monotonic_us();

// Writes to stdout, klog and a bounded native log in the title's download area.
void log(const char *format, ...) __attribute__((format(printf, 1, 2)));
// Start-up is over: from now on a thread writes the log lines, so a busy hard
// drive no longer holds up the caller (up to 200 ms per line on the PS4).
// Until then every line is on the disk before log() returns.
void log_started();

// Hides the system splash screen. PS4 Piglet needs this before the first swap.
bool hide_splash_screen();

void sleep_us(std::uint32_t microseconds);

// Never returns: keeps the title alive until the shell closes it. Returning
// from main or calling exit() is not safe for a native title.
[[noreturn]] void park();

// Ends the title the one way that is clean for a native app: the system is
// asked to close it (exit() and _Exit() end in the crash reporter instead).
// Parks if the request is refused.
[[noreturn]] void quit();

// A startup failure must remain visible even when EGL/shaders cannot render.
// Uses the system message dialog, then asks the shell to close the title.
[[noreturn]] void startup_failure(const char *reason);

} // namespace hui::sys
