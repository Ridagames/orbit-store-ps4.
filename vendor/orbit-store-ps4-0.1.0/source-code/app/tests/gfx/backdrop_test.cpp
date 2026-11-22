// Exercise real ES2 shader arithmetic, including mediump overflow at TV sizes.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/backdrop.hpp"
#include "gfx/gl_batch.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <array>
#include <cstdio>
#include <vector>

int main()
{
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = platform ? platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                                  : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr) ||
        !eglBindAPI(EGL_OPENGL_ES_API)) return 2;
    const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return 2;
    int result = 0;
    {
        hui::gfx::Backdrop backdrop;
        if (!backdrop.init()) return 2;
        for (const auto size : {std::array<int, 2>{1920, 1080}, std::array<int, 2>{3840, 2160}})
        {
            GLuint texture = 0, framebuffer = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size[0], size[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 2;
            std::vector<unsigned char> pixels(static_cast<std::size_t>(size[0]) * size[1] * 4);
            for (const auto mode : {hui::gfx::BackdropMode::gradient, hui::gfx::BackdropMode::waves})
                for (const float time : {0.0f, 10.8f, 59.5f})
                {
                    hui::gfx::BackdropSpec spec;
                    spec.mode = mode;
                    spec.time = time;
                    for (auto &color : spec.colors) color = hui::gfx::Color::rgb(0x4488bb);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                    backdrop.draw(spec, size[0], size[1]);
                    glReadPixels(0, 0, size[0], size[1], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    if (glGetError() != GL_NO_ERROR) return 2;
                    // All these modes paint the entire target. A grid includes
                    // its centre and far edges, where 16-bit hash arithmetic failed.
                    int dark = 0;
                    for (int y = 1; y < size[1]; y += size[1] / 12)
                        for (int x = 1; x < size[0]; x += size[0] / 16)
                        {
                            const auto at = (static_cast<std::size_t>(y) * size[0] + x) * 4;
                            if (pixels[at] < 16 || pixels[at + 1] < 16 || pixels[at + 2] < 16) ++dark;
                        }
                    std::printf("%dx%d mode=%d time=%.1f unexpected dark samples=%d\n",
                                size[0], size[1], static_cast<int>(mode), time, dark);
                    if (dark != 0) result = 1;
                }
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteTextures(1, &texture);
        }
    }
    {
        // Check texel-centre sampling near the end of a 4096-row atlas.
        hui::gfx::GlBatch batch;
        if (!batch.init()) return 2;
        constexpr int width = 256, atlas_height = 4096, first_row = 3800;
        std::vector<unsigned char> atlas(width * atlas_height * 4, 255);
        for (int y = 0; y < atlas_height; ++y)
            for (int x = 0; x < width; ++x)
                for (int c = 0; c < 3; ++c)
                    atlas[(y * width + x) * 4 + c] = y % 2 ? 255 : 0;
        const GLuint source = batch.create_texture(width, atlas_height, atlas.data());
        GLuint target = 0, framebuffer = 0;
        glGenTextures(1, &target);
        glBindTexture(GL_TEXTURE_2D, target);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, width, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 2;
        hui::gfx::DrawList list;
        list.image(source, {0, 0, width, width},
                   {0, static_cast<float>(first_row) / atlas_height, 1, static_cast<float>(width) / atlas_height},
                   hui::gfx::Color::rgb(0xffffff));
        batch.draw(list, hui::gfx::fit_viewport(width, width, width, width), width, width);
        std::vector<unsigned char> pixels(width * width * 4);
        glReadPixels(0, 0, width, width, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (glGetError() != GL_NO_ERROR) return 2;
        int mismatches = 0;
        for (int y = 0; y < width; ++y)
        {
            const int actual = pixels[((width - 1 - y) * width + width / 2) * 4];
            const int expected = (first_row + y) % 2 ? 255 : 0;
            if (actual < expected - 3 || actual > expected + 3) ++mismatches;
        }
        std::printf("4096-row atlas: incorrect texel rows=%d\n", mismatches);
        if (mismatches != 0) result = 1;
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &target);
        glDeleteTextures(1, &source);
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return result;
}
