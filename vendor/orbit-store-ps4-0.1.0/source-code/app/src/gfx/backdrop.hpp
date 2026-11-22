// ps5-homebrew-ui - Procedural full-screen backdrops and post overlays.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/backdrop_spec.hpp"
#include "gfx/gl_headers.hpp"

namespace hui::gfx
{

// One program draws every mode over a full-screen triangle.
class Backdrop
{
  public:
    Backdrop() = default;
    Backdrop(const Backdrop &) = delete;
    Backdrop &operator=(const Backdrop &) = delete;
    ~Backdrop();

    bool init();
    void release();
    // Draws into the bound framebuffer. opacity < 1 blends over what is there
    // (used to cross-fade two backdrops); post overlays always blend.
    void draw(const BackdropSpec &spec, int width, int height, float opacity = 1.0f);

  private:
    GLuint program_ = 0;
    GLint u_resolution_ = -1;
    GLint u_time_ = -1;
    GLint u_mode_ = -1;
    GLint u_colors_ = -1;
    GLint u_params_ = -1;
    GLint u_opacity_ = -1;
};

} // namespace hui::gfx
