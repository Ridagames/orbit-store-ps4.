// ps4-homebrew-ui - EGL display and OpenGL ES 2 context.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <EGL/egl.h>

namespace hui::ps4
{

// Owns the EGL display, window surface and ES 2 context. Piglet presents
// fullscreen at the surface's own size, so changing the size means close()
// and open() again. Every GL object dies with the context and must be
// recreated by its owner.
class Display
{
  public:
    Display() = default;
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;
    ~Display();

    // Opens the fullscreen ES 2 surface (Piglet fills the TV mode); the
    // arguments are accepted for interface compatibility and ignored.
    bool open(int width = 1920, int height = 1080);
    bool swap();
    void close();
    // Piglet always owns the mode; false.
    static bool supports_display_modes();

    int width() const
    {
        return width_;
    }
    int height() const
    {
        return height_;
    }
    EGLint last_error() const
    {
        return last_error_;
    }

  private:
    void fail(const char *operation);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    // Piglet retains the native window. Keep the full 16-byte ABI alive with
    // the surface instead of passing a three-int object on open()'s stack.
    int window_[4] = {};
    int width_ = 0;
    int height_ = 0;
    EGLint last_error_ = EGL_SUCCESS;
};

const char *egl_error_name(EGLint error);

} // namespace hui::ps4
