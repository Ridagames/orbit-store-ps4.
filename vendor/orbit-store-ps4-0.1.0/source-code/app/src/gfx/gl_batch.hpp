// ps5-homebrew-ui - OpenGL ES 2 backend for the 2D draw list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "gfx/gl_headers.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hui::gfx
{

// One expanded quad vertex: the corner plus the whole Instance, so the ES2
// pipeline sees plain per-vertex attributes (no divisors, no instancing).
struct QuadVertex
{
    float corner[2];
    float rect[4];
    float top[4];
    float bottom[4];
    float border[4];
    float params[4];
    float extra[4];
};
static_assert(sizeof(QuadVertex) == 104);

// Draws a DrawList with one quad program: instances are expanded into six
// vertices each on the CPU (ES2 has no instancing), uploaded once per frame
// into an orphaned stream buffer, then each run is one glDrawArrays call
// (triangle lists only).
class GlBatch
{
  public:
    GlBatch() = default;
    GlBatch(const GlBatch &) = delete;
    GlBatch &operator=(const GlBatch &) = delete;
    ~GlBatch();

    bool init();
    // Deletes the programs, vertex arrays and buffers; init() recreates them.
    void release();
    // Uploads a font atlas as a single-level R8 texture and gives it one of
    // the font slots; returns the handle DrawList::text takes. With every
    // slot taken it returns a plain texture name, which also works but starts
    // a new draw call wherever that font is used.
    std::uint32_t create_font_texture(const Font &font);
    // Uploads the language's fallback font (CJK, Arabic) to the reserved
    // slot, replacing the previous one; nullptr only frees it. Returns the
    // handle, or 0.
    std::uint32_t set_fallback_font(Font *font);
    // Uploads RGBA8 pixels as a single-level texture; returns its name.
    std::uint32_t create_texture(int width, int height, const std::uint8_t *rgba);

    // Draws into the currently bound framebuffer of the given size.
    void draw(const DrawList &list, const Viewport &viewport, int surface_width,
              int surface_height);

    std::size_t last_draw_calls() const
    {
        return draw_calls_;
    }

  private:
    void bind_quad_attribs() const;
    void bind_mesh_attribs() const;

    GLuint program_ = 0;
    GLuint buffer_ = 0;
    std::size_t capacity_ = 0; // expanded vertices
    GLuint mesh_program_ = 0;
    GLuint mesh_buffer_ = 0;
    std::size_t mesh_capacity_ = 0; // vertices
    GLint u_viewport_ = -1;
    GLint u_surface_ = -1;
    GLint u_texture_ = -1;
    GLint u_mesh_viewport_ = -1;
    GLint u_mesh_surface_ = -1;
    std::size_t draw_calls_ = 0;
    GLuint font_textures_[kFontSlots] = {};
    std::vector<GLuint> fallback_pages_, extra_fonts_;
    std::uint32_t font_count_ = 0;
    std::vector<QuadVertex> scratch_;
};

} // namespace hui::gfx
