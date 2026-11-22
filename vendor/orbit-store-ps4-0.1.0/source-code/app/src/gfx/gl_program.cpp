// ps5-homebrew-ui - GLSL program compilation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/gl_program.hpp"
#include "gfx/shader_binary.hpp"

#include <cstdint>
#if defined(ORBIT_CAPTURE_SHADERS)
#include <EGL/egl.h>
#include <cstdio>
#include <sys/stat.h>
#endif

#include "platform/ps4/system.hpp"

namespace hui::gfx
{

namespace
{

// GLSL ES 1.00 (Piglet, and Mesa's ES 2.0 in the preview). The fragment
// prefix enables standard derivatives when the driver has them; shaders that
// can degrade gracefully guard fwidth() with #ifdef.
const char *g_vertex_prefix = "#version 100\nprecision highp float;\n";
const char *g_fragment_prefix = "#version 100\n"
                                "#extension GL_OES_standard_derivatives : enable\n"
                                "precision mediump float;\n";

GLuint compile(const char *label, GLenum stage, const char *source)
{
    const char *sources[] = {stage == GL_VERTEX_SHADER ? g_vertex_prefix : g_fragment_prefix,
                             source};
    GLuint shader = glCreateShader(stage);
#if defined(ORBIT_PS4)
    const std::string name = std::string(label) + (stage == GL_VERTEX_SHADER ? ".vert" : ".frag");
    const std::string text = std::string(sources[0]) + source;
    std::uint32_t format = 0;
    std::vector<std::uint8_t> binary;
    if (read_shader_binary("/app0/assets/shaders/" + name, text, &format, &binary)) {
        glShaderBinary(1, &shader, format, binary.data(), static_cast<GLsizei>(binary.size()));
        GLint loaded = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &loaded);
        if (loaded == GL_TRUE) return shader;
        sys::log("[HUI] Piglet rejected packaged shader %s", name.c_str());
    }
    GLboolean compiler = GL_FALSE;
    glGetBooleanv(GL_SHADER_COMPILER, &compiler);
    if (compiler != GL_TRUE) {
        sys::log("[HUI] missing/stale Piglet shader %s; retail firmware has no GLSL compiler", name.c_str());
        glDeleteShader(shader);
        return 0;
    }
#endif
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE)
    {
#if defined(ORBIT_PS4) && defined(ORBIT_CAPTURE_SHADERS)
        // Development capture only, on a system with a working Piglet compiler.
        // The format comes from Piglet itself, never from a guessed constant.
        using Capture = void (*)(GLuint, GLsizei, GLsizei *, GLenum *, void *);
        auto capture = reinterpret_cast<Capture>(eglGetProcAddress("glPigletGetShaderBinarySCE"));
        if (capture) {
            std::vector<std::uint8_t> bytes(1024 * 1024);
            GLsizei size = 0; GLenum captured_format = 0;
            capture(shader, static_cast<GLsizei>(bytes.size()), &size, &captured_format, bytes.data());
            if (size > 0 && static_cast<std::size_t>(size) <= bytes.size()) {
                mkdir("/download0/orbit-store", 0755);
                mkdir("/download0/orbit-store/shaders", 0755);
                const std::string base = "/download0/orbit-store/shaders/" + name;
                const bool saved = write_shader_binary(base, text, captured_format,
                                                        bytes.data(), static_cast<std::size_t>(size));
                sys::log("[HUI] shader capture %s: %s", name.c_str(), saved ? "saved" : "failed");
            }
        }
#endif
        return shader;
    }
    char info[512] = {};
    glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
    sys::log("[HUI] shader %s (%s) failed: %s", label,
             stage == GL_VERTEX_SHADER ? "vertex" : "fragment", info);
    glDeleteShader(shader);
    return 0;
}

} // namespace

GLuint build_program(const char *label, const char *vertex_source, const char *fragment_source,
                     std::initializer_list<const char *> attribs)
{
    // Start-up is mostly this: each program is logged with its time.
    const std::int64_t started = sys::monotonic_us();
    GLuint vertex = compile(label, GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = vertex != 0 ? compile(label, GL_FRAGMENT_SHADER, fragment_source) : 0;
    if (fragment == 0)
    {
        if (vertex != 0)
            glDeleteShader(vertex);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    GLuint location = 0;
    for (const char *attrib : attribs)
        glBindAttribLocation(program, location++, attrib);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    sys::log("[HUI] program %s built in %lld ms", label,
             static_cast<long long>((sys::monotonic_us() - started) / 1000));
    if (ok == GL_TRUE)
        return program;
    char info[512] = {};
    glGetProgramInfoLog(program, sizeof(info), nullptr, info);
    sys::log("[HUI] program %s link failed: %s", label, info);
    glDeleteProgram(program);
    return 0;
}

namespace
{

GLuint g_fullscreen_buffer = 0;

} // namespace

void draw_fullscreen_triangle(GLuint attrib)
{
    GLuint &buffer = g_fullscreen_buffer;
    if (buffer == 0)
    {
        const float positions[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
    }
    glEnableVertexAttribArray(attrib);
    glVertexAttribPointer(attrib, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void reset_gl_helpers()
{
    // The buffer lives in the GL context, not here: when the context dies the
    // name is dead weight, and recreating costs one small VBO.
    g_fullscreen_buffer = 0;
}

} // namespace hui::gfx
