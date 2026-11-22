// ps5-homebrew-ui - GLSL program compilation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/gl_headers.hpp"

#include <initializer_list>

namespace hui::gfx
{

// Compiles and links a vertex/fragment program. attribs[i] is bound to
// attribute location i before linking, so callers keep a stable layout.
// Returns 0 and logs the info log on failure.
GLuint build_program(const char *label, const char *vertex_source, const char *fragment_source,
                     std::initializer_list<const char *> attribs = {});

// Draws the shared full-screen triangle through attribute attrib, a vec2
// position stream of (-1,-1), (3,-1), (-1,3). The buffer is created on
// first use. The caller's program stays bound.
void draw_fullscreen_triangle(GLuint attrib);

// Drops the shared fullscreen buffer's handle, so the next draw recreates it
// in the current context. Call when the GL context itself was destroyed
// (display close/open); Renderer::release calls it for every teardown.
void reset_gl_helpers();

} // namespace hui::gfx
