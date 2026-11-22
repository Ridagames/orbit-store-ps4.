// ps4-homebrew-ui - EGL display and OpenGL ES 2 context through Piglet.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bring-up order follows the OpenOrbis Piglet convention: flexible GPU
// memory is allocated and mapped, scePigletSetConfigurationVSH hands it to
// the driver, then ordinary EGL 1.4 creates an ES 2 context. Piglet owns
// the swap chain: the surface fills the TV resolution and the app renders
// into it at the queried size.

#include "platform/ps4/display_egl.hpp"
#include "platform/ps4/piglet_config.hpp"

#include "platform/ps4/system.hpp"

#include "gfx/gl_headers.hpp"

#include <EGL/eglext.h>

#include <cstdint>
#include <cstring>

namespace
{

using hui::ps4::ScePglConfig;

// Piglet configuration and its EGL display last for the process lifetime.
// Reconfiguration/eglTerminate followed by reinitialization breaks Piglet.
// Reference: ioQuake3-PS4/code/ps4/ps4_glimp.c (console-tested by its author).
bool piglet_configured = false;
EGLDisplay piglet_display = EGL_NO_DISPLAY;
bool piglet_display_initialized = false;

constexpr EGLint kConfigAttributes[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE,      8,                EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,     8,                EGL_ALPHA_SIZE,      8,
    EGL_DEPTH_SIZE,    0,                EGL_STENCIL_SIZE,    0,
    EGL_NONE,
};

constexpr EGLint kContextAttributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE,
};

extern "C"
{
    bool scePigletSetConfigurationVSH(const ScePglConfig *config);
}

const char *gl_string(GLenum name)
{
    const auto *value = reinterpret_cast<const char *>(glGetString(name));
    return value != nullptr ? value : "(null)";
}

// Memory sizes are the proven values from flat_z's reference bring-up: the
// flexible-memory cap (170 MB) gives Piglet room for a 1080p swap chain,
// the SDF atlases and the glass render targets.
bool configure_piglet(int width, int height)
{
    if (piglet_configured) return true;
    const auto config = hui::ps4::piglet_configuration(width, height);
    hui::sys::log("[HUI] configuring Piglet %dx%d", width, height);
    if (!scePigletSetConfigurationVSH(&config))
    {
        hui::sys::log("[HUI] scePigletSetConfigurationVSH failed");
        return false;
    }
    piglet_configured = true;
    return true;
}

} // namespace

namespace hui::ps4
{

Display::~Display()
{
    close();
}

void Display::fail(const char *operation)
{
    last_error_ = eglGetError();
    sys::log("[HUI] egl %s failed: %s (0x%04x)", operation, egl_error_name(last_error_),
             static_cast<unsigned>(last_error_));
}

bool Display::supports_display_modes()
{
    // Piglet scales its fixed surface to the TV mode itself.
    return false;
}

bool Display::open(int width, int height)
{
    close();
    EGLint major = 0;
    EGLint minor = 0;
    EGLint count = 0;
    EGLConfig config = nullptr;
    if (width <= 0)
        width = 1920;
    if (height <= 0)
        height = 1080;

    if (!configure_piglet(width, height))
        return false;

    if (piglet_display == EGL_NO_DISPLAY)
    {
        sys::log("[HUI] opening EGL display");
        piglet_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    display_ = piglet_display;
    if (display_ == EGL_NO_DISPLAY)
    {
        fail("eglGetDisplay");
        return false;
    }
    sys::log("[HUI] initializing EGL display");
    if (!piglet_display_initialized && !eglInitialize(display_, &major, &minor))
    {
        fail("eglInitialize");
        close();
        return false;
    }
    piglet_display_initialized = true;
    sys::log("[HUI] selecting EGL configuration");
    if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        fail("eglBindAPI");
        close();
        return false;
    }
    if (!eglChooseConfig(display_, kConfigAttributes, &config, 1, &count) || count != 1)
    {
        fail("eglChooseConfig");
        close();
        return false;
    }
    // Full native-window ABI: id, width, height, reserved; id 0 is the main plane.
    static_assert(sizeof(window_) == 16);
    window_[0] = window_[3] = 0; window_[1] = width; window_[2] = height;
    sys::log("[HUI] creating EGL surface");
    surface_ = eglCreateWindowSurface(display_, config,
                                    reinterpret_cast<EGLNativeWindowType>(window_), nullptr);
    if (surface_ == EGL_NO_SURFACE)
    {
        fail("eglCreateWindowSurface");
        close();
        return false;
    }
    sys::log("[HUI] creating EGL context");
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, kContextAttributes);
    if (context_ == EGL_NO_CONTEXT)
    {
        fail("eglCreateContext");
        close();
        return false;
    }
    sys::log("[HUI] binding EGL context");
    if (!eglMakeCurrent(display_, surface_, surface_, context_))
    {
        fail("eglMakeCurrent");
        close();
        return false;
    }
    if (!eglQuerySurface(display_, surface_, EGL_WIDTH, &width_) ||
        !eglQuerySurface(display_, surface_, EGL_HEIGHT, &height_) || width_ <= 0 || height_ <= 0)
    {
        fail("eglQuerySurface");
        close();
        return false;
    }
    // Retail Piglet on the tested PS4 blocks inside swap with interval 1.
    // Use the reference sample's interval 0; main paces rendering to 60 Hz.
    if (!eglSwapInterval(display_, 0))
    {
        fail("eglSwapInterval");
        close();
        return false;
    }

    sys::log("[HUI] egl %d.%d surface %dx%d", major, minor, width_, height_);
    sys::log("[HUI] gl version=\"%s\" renderer=\"%s\" glsl=\"%s\"", gl_string(GL_VERSION),
             gl_string(GL_RENDERER), gl_string(GL_SHADING_LANGUAGE_VERSION));
    GLint max_texture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    sys::log("[HUI] gl max_texture=%d", static_cast<int>(max_texture));
    return true;
}

bool Display::swap()
{
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE)
        return false;
    if (!eglSwapBuffers(display_, surface_))
    {
        fail("eglSwapBuffers");
        return false;
    }
    return true;
}

void Display::close()
{
    if (display_ != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT)
            eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE)
            eglDestroySurface(display_, surface_);
        // Piglet cannot reinitialize a terminated EGL display in this process.
    }
    display_ = EGL_NO_DISPLAY;
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
    width_ = height_ = 0;
}

const char *egl_error_name(EGLint error)
{
    switch (error)
    {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE:
        return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:
        return "EGL_CONTEXT_LOST";
    default:
        return "unknown";
    }
}

} // namespace hui::ps4
