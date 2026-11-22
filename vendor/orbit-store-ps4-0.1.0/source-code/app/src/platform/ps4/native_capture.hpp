// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef ORBIT_NATIVE_CAPTURE
#include "core/input.hpp"
namespace hui::ps4 {
// Private hardware tester only. Captures Orbit's GL frame, not system dialogs.
void capture_requested_frame(int width, int height);
// Private navigation check only; no arbitrary commands or controller injection.
void read_requested_input(InputFrame &input);
}
#endif
