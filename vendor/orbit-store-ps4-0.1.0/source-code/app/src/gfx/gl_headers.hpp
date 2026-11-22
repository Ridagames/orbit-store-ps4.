// ps5-homebrew-ui - GL headers for the renderer backends.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PS4 renders through Piglet's OpenGL ES 2.0; the desktop preview creates a
// real ES 2.0 context through EGL so it exercises the same shader code.
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
