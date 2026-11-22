// ps5-homebrew-ui - OpenGL ES 2 backend for the 2D draw list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/gl_batch.hpp"

#include "gfx/gl_program.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::gfx
{

namespace
{

// ES 2 has no instancing and no gl_VertexID, so quads are expanded on the
// CPU: six vertices per instance, each carrying the corner and the full
// instance fields. The varying highp count is packed to stay under the ES2 floor
// of eight vec4 vectors.
constexpr const char *kVertex = R"(
attribute vec2 a_corner;
attribute vec4 a_rect;
attribute vec4 a_top;
attribute vec4 a_bottom;
attribute vec4 a_border;
attribute vec4 a_params;
attribute vec4 a_extra;
uniform highp vec4 u_viewport; // scale, offset x, offset y
uniform highp vec2 u_surface;
varying highp vec4 v_pack0; // local centred xy, half size
varying highp vec4 v_pack1; // virtual xy, uv
varying highp vec4 v_top;
varying highp vec4 v_bottom;
varying highp vec4 v_border;
varying highp vec4 v_params;
varying highp vec4 v_extra;
void main()
{
    vec2 corner = a_corner;
    float shape = a_params.w;
    vec2 size = a_rect.zw;
    // Grow shapes by their softness plus one output pixel for anti-aliasing;
    // glyph and image quads map texels exactly and are not grown.
    float pad = ((shape > 0.5 && shape < 1.5) || (shape > 2.5 && shape < 3.5))
                    ? 0.0
                    : a_params.z + 1.0 / u_viewport.x;
    vec2 local = mix(vec2(-pad), size + vec2(pad), corner);
    vec2 centred = local - 0.5 * size;
    vec2 virt = a_rect.xy + local;
    // Rounded rectangles, shadows and triangles may turn about their centre: the quad
    // rotates, the distance field keeps working in the unrotated frame.
    bool box = shape < 0.5 || (shape > 1.5 && shape < 2.5) || (shape > 4.5 && shape < 5.5);
    if (box && a_extra.x != 0.0)
    {
        float c = cos(a_extra.x);
        float s = sin(a_extra.x);
        virt = a_rect.xy + 0.5 * size + vec2(c * centred.x - s * centred.y,
                                             s * centred.x + c * centred.y);
    }
    v_pack0 = vec4(centred, 0.5 * size);
    v_pack1 = vec4(virt, mix(a_extra.xy, a_extra.zw, corner));
    v_top = a_top;
    v_bottom = a_bottom;
    v_border = a_border;
    v_params = a_params;
    v_extra = a_extra;
    vec2 surface = virt * u_viewport.x + u_viewport.yz;
    vec2 ndc = surface / u_surface * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

constexpr const char *kFragment = R"(
uniform highp vec4 u_viewport;
uniform sampler2D u_texture;
uniform sampler2D u_font0;
uniform sampler2D u_font1;
uniform sampler2D u_font2;
uniform sampler2D u_font3;
uniform sampler2D u_font4;
uniform sampler2D u_font5;
uniform sampler2D u_font6;
varying highp vec4 v_pack0;
varying highp vec4 v_pack1;
varying highp vec4 v_top;
varying highp vec4 v_bottom;
varying highp vec4 v_border;
varying highp vec4 v_params;
varying highp vec4 v_extra;

float round_box(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float triangle(vec2 p, vec2 p0, vec2 p1, vec2 p2)
{
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 q0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 q1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 q2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(q0, q0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(q1, q1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(q2, q2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

float star5(vec2 p, float r, float rf)
{
    const vec2 k1 = vec2(0.809016994375, -0.587785252292);
    const vec2 k2 = vec2(-k1.x, k1.y);
    p.x = abs(p.x);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= r;
    vec2 ba = rf * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, r);
    return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}

void main()
{
    vec2 v_local = v_pack0.xy;
    vec2 v_half = v_pack0.zw;
    vec2 v_virtual = v_pack1.xy;
    vec2 v_uv = v_pack1.zw;
    // Gradient axis of the pre-centred local coordinate, recomputed from the
    // packed varyings to stay inside the ES2 varying highp budget.
    float v_t = (v_params.w < 0.5 && v_extra.y > 0.5)
                    ? clamp((v_local.x + v_half.x) / max(2.0 * v_half.x, 1e-3), 0.0, 1.0)
                    : clamp((v_local.y + v_half.y) / max(2.0 * v_half.y, 1e-3), 0.0, 1.0);
    float px = 1.0 / u_viewport.x; // virtual units per output pixel
    int shape = int(v_params.w + 0.5);
    vec4 fill = mix(v_top, v_bottom, v_t);
    vec4 color;
    if (shape == 0)
    {
        float radius = min(v_params.x, min(v_half.x, v_half.y));
        float d = round_box(v_local, v_half, radius);
        color = fill;
        if (v_params.y > 0.0)
        {
            float inner = clamp(0.5 - (d + v_params.y) / px, 0.0, 1.0);
            color = mix(v_border, fill, inner);
        }
        color.a *= clamp(0.5 - d / px, 0.0, 1.0);
    }
    else if (shape == 1)
    {
        // The font atlases stay bound to their own units; the slot picks one.
        int slot = int(v_params.z + 0.5);
        float sdf;
        if (slot == 1)
            sdf = texture2D(u_font0, v_uv).r;
        else if (slot == 2)
            sdf = texture2D(u_font1, v_uv).r;
        else if (slot == 3)
            sdf = texture2D(u_font2, v_uv).r;
        else if (slot == 4)
            sdf = texture2D(u_font3, v_uv).r;
        else if (slot == 5)
            sdf = texture2D(u_font4, v_uv).r;
        else if (slot == 6)
            sdf = texture2D(u_font5, v_uv).r;
        else if (slot == 7)
            sdf = texture2D(u_font6, v_uv).r;
        else
            sdf = texture2D(u_texture, v_uv).r;
        float dist = (sdf - 0.5) * 2.0 * v_params.x;
        color = vec4(fill.rgb, fill.a * clamp(dist / px + 0.5, 0.0, 1.0));
    }
    else if (shape == 2)
    {
        float radius = min(v_params.x, min(v_half.x, v_half.y));
        float d = round_box(v_local, v_half, radius);
        color = vec4(fill.rgb, fill.a * (1.0 - smoothstep(-v_params.z, v_params.z, d)));
    }
    else if (shape == 3)
    {
        color = texture2D(u_texture, v_uv) * fill;
        if (v_params.x > 0.0)
        {
            float radius = min(v_params.x, min(v_half.x, v_half.y));
            color.a *= clamp(0.5 - round_box(v_local, v_half, radius) / px, 0.0, 1.0);
        }
    }
    else if (shape == 4)
    {
        vec2 pa = v_virtual - v_extra.xy;
        vec2 ba = v_extra.zw - v_extra.xy;
        float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
        float d = length(pa - ba * h) - 0.5 * v_params.y;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    else if (shape == 8)
    {
        // Box with 45 degree corner cuts: the box distance, limited by the
        // diagonal plane through each corner.
        float cut = min(v_params.x, min(v_half.x, v_half.y));
        vec2 q = abs(v_local) - v_half;
        float box = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
        float d = max(box, (q.x + q.y + cut) * 0.70710678);
        color = fill;
        if (v_params.y > 0.0)
        {
            float inner = clamp(0.5 - (d + v_params.y) / px, 0.0, 1.0);
            color = mix(v_border, fill, inner);
        }
        color.a *= clamp(0.5 - d / px, 0.0, 1.0);
    }
    else if (shape == 7)
    {
        // Ring sector, after Inigo Quilez's arc: turn the sector upright,
        // fold it on its axis, then measure to the ring or to its end.
        float rb = 0.5 * v_params.y;
        float ra = min(v_half.x, v_half.y) - rb;
        float half_sweep = 0.5 * v_extra.x;
        float mid = v_params.x + half_sweep;
        float cm = cos(mid);
        float sm = sin(mid);
        vec2 p = vec2(v_local.x, -v_local.y);
        p = vec2(abs(p.x * cm - p.y * sm), p.x * sm + p.y * cm);
        vec2 sc = vec2(sin(half_sweep), cos(half_sweep));
        float ring = abs(length(p) - ra);
        float d;
        if (v_extra.y > 0.5)
            d = max(ring - rb, p.x * sc.y - p.y * sc.x);
        else
            d = ((sc.y * p.x > sc.x * p.y) ? length(p - sc * ra) : ring) - rb;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    else if (shape == 6)
    {
        vec2 p = vec2(v_local.x, -v_local.y);
        float d = star5(p, min(v_half.x, v_half.y), 0.45);
        if (v_params.y > 0.0)
            d = abs(d + 0.5 * v_params.y) - 0.5 * v_params.y;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    else
    {
        float d = triangle(v_local, vec2(0.0, -v_half.y), vec2(-v_half.x, v_half.y),
                           vec2(v_half.x, v_half.y));
        if (v_params.y > 0.0)
            d = abs(d + 0.5 * v_params.y) - 0.5 * v_params.y;
        color = vec4(fill.rgb, fill.a * clamp(0.5 - d / px, 0.0, 1.0));
    }
    if (color.a <= 0.0)
        discard;
    gl_FragColor = color;
}
)";

constexpr const char *kMeshVertex = R"(
attribute vec2 a_position;
attribute vec4 a_color;
uniform highp vec4 u_viewport;
uniform highp vec2 u_surface;
varying highp vec4 v_color;
void main()
{
    vec2 surface = a_position * u_viewport.x + u_viewport.yz;
    vec2 ndc = surface / u_surface * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_color = a_color;
}
)";

constexpr const char *kMeshFragment = R"(
varying highp vec4 v_color;
void main()
{
    gl_FragColor = v_color;
}
)";

constexpr float kCorners[6][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
                                  {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

GLuint upload_font(const Font &font, std::size_t page = 0)
{
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (font.atlas_width(page) > limit || font.atlas_height(page) > limit || !font.atlas_pixels(page))
    {
        std::fprintf(stderr, "Font atlas %ux%u exceeds GL_MAX_TEXTURE_SIZE=%d\n",
                     font.atlas_width(), font.atlas_height(), limit);
        return 0;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    (void)glGetError();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, font.atlas_width(page), font.atlas_height(page), 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, font.atlas_pixels(page));
    if (glGetError() != GL_NO_ERROR) { glDeleteTextures(1, &texture); return 0; }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

} // namespace

GlBatch::~GlBatch()
{
    release();
}

void GlBatch::release()
{
    if (buffer_ != 0)
        glDeleteBuffers(1, &buffer_);
    if (program_ != 0)
        glDeleteProgram(program_);
    if (mesh_buffer_ != 0)
        glDeleteBuffers(1, &mesh_buffer_);
    if (mesh_program_ != 0)
        glDeleteProgram(mesh_program_);
    for (GLuint &texture : font_textures_)
    {
        if (texture != 0)
            glDeleteTextures(1, &texture);
        texture = 0;
    }
    font_count_ = 0;
    for (GLuint texture : fallback_pages_) glDeleteTextures(1, &texture);
    for (GLuint texture : extra_fonts_) glDeleteTextures(1, &texture);
    fallback_pages_.clear(); extra_fonts_.clear();
    buffer_ = program_ = mesh_buffer_ = mesh_program_ = 0;
    capacity_ = 0;
    mesh_capacity_ = 0;
    scratch_.clear();
    scratch_.shrink_to_fit();
}

bool GlBatch::init()
{
    program_ = build_program("batch2d", kVertex, kFragment,
                             {"a_corner", "a_rect", "a_top", "a_bottom", "a_border", "a_params",
                              "a_extra"});
    mesh_program_ = build_program("mesh2d", kMeshVertex, kMeshFragment,
                                  {"a_position", "a_color"});
    if (program_ == 0 || mesh_program_ == 0)
        return false;
    u_viewport_ = glGetUniformLocation(program_, "u_viewport");
    u_surface_ = glGetUniformLocation(program_, "u_surface");
    u_texture_ = glGetUniformLocation(program_, "u_texture");
    u_mesh_viewport_ = glGetUniformLocation(mesh_program_, "u_viewport");
    u_mesh_surface_ = glGetUniformLocation(mesh_program_, "u_surface");
    glUseProgram(program_);
    glUniform1i(u_texture_, 0);
    // Font atlases stay on units 1..7 for the app's lifetime.
    const char *font_names[kFontSlots] = {"u_font0", "u_font1", "u_font2", "u_font3",
                                          "u_font4", "u_font5", "u_font6"};
    for (std::uint32_t slot = 0; slot < kFontSlots; ++slot)
    {
        const GLint location = glGetUniformLocation(program_, font_names[slot]);
        if (location >= 0)
            glUniform1i(location, static_cast<GLint>(slot + 1));
    }
    glGenBuffers(1, &mesh_buffer_);
    glGenBuffers(1, &buffer_);
    return true;
}

std::uint32_t GlBatch::create_font_texture(const Font &font)
{
    const GLuint texture = upload_font(font);
    if (texture == 0)
        return 0;
    if (font_count_ < kFallbackSlot - 1)
    {
        font_textures_[font_count_] = texture;
        ++font_count_;
        return kFontHandleBase | font_count_;
    }
    extra_fonts_.push_back(texture);
    return texture;
}

std::uint32_t GlBatch::set_fallback_font(Font *font)
{
    for (GLuint texture : fallback_pages_) glDeleteTextures(1, &texture);
    fallback_pages_.clear();
    GLuint &slot = font_textures_[kFallbackSlot - 1];
    if (slot != 0)
        glDeleteTextures(1, &slot);
    slot = 0;
    if (font == nullptr || font->atlas_pixels() == nullptr)
        return 0;
    font->set_page_textures({});
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (limit <= 0 || !font->paginate(static_cast<std::uint16_t>(std::min(limit, 4096)))) return 0;
    if (font->page_count() > 1)
    {
        for (std::size_t page = 0; page < font->page_count(); ++page)
        {
            GLuint texture = upload_font(*font, page);
            if (!texture) {
                for (GLuint previous : fallback_pages_) glDeleteTextures(1, &previous);
                fallback_pages_.clear(); return 0;
            }
            fallback_pages_.push_back(texture);
        }
        font->set_page_textures(fallback_pages_);
        return fallback_pages_.front();
    }
    slot = upload_font(*font);
    return slot == 0 ? 0 : kFontHandleBase | kFallbackSlot;
}

std::uint32_t GlBatch::create_texture(int width, int height, const std::uint8_t *rgba)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

void GlBatch::bind_quad_attribs() const
{
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    const GLsizei stride = sizeof(QuadVertex);
    for (GLuint attribute = 0; attribute < 7; ++attribute)
        glEnableVertexAttribArray(attribute);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    for (GLuint attribute = 1; attribute < 7; ++attribute)
        glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void *>(
                                  static_cast<std::uintptr_t>(8 + (attribute - 1) * 16)));
}

void GlBatch::bind_mesh_attribs() const
{
    glBindBuffer(GL_ARRAY_BUFFER, mesh_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<const void *>(static_cast<std::uintptr_t>(8)));
    for (GLuint attribute = 2; attribute < 7; ++attribute)
        glDisableVertexAttribArray(attribute);
}

void GlBatch::draw(const DrawList &list, const Viewport &viewport, int surface_width,
                   int surface_height)
{
    draw_calls_ = 0;
    const auto &instances = list.instances();
    const auto &mesh = list.mesh_vertices();
    if (instances.empty() && mesh.empty())
        return;

    if (!instances.empty())
    {
        // Expand six vertices per quad; ES2 has no instanced draw.
        scratch_.resize(instances.size() * 6);
        QuadVertex *out = scratch_.data();
        for (const Instance &instance : instances)
        {
            for (int vertex = 0; vertex < 6; ++vertex, ++out)
            {
                out->corner[0] = kCorners[vertex][0];
                out->corner[1] = kCorners[vertex][1];
                std::copy(instance.rect, instance.rect + 4, out->rect);
                std::copy(instance.color_top, instance.color_top + 4, out->top);
                std::copy(instance.color_bottom, instance.color_bottom + 4, out->bottom);
                std::copy(instance.border_color, instance.border_color + 4, out->border);
                std::copy(instance.params, instance.params + 4, out->params);
                std::copy(instance.extra, instance.extra + 4, out->extra);
            }
        }
        // Orphan, then fill: re-specifying storage avoids waiting on the GPU's
        // reads of the previous frame.
        capacity_ = std::max(capacity_, scratch_.size());
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_ * sizeof(QuadVertex)),
                     nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(scratch_.size() * sizeof(QuadVertex)),
                        scratch_.data());
    }
    if (!mesh.empty())
    {
        mesh_capacity_ = std::max(mesh_capacity_, mesh.size());
        glBindBuffer(GL_ARRAY_BUFFER, mesh_buffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh_capacity_ * sizeof(MeshVertex)),
                     nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(mesh.size() * sizeof(MeshVertex)), mesh.data());
    }

    glViewport(0, 0, surface_width, surface_height);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(program_);
    glUniform4f(u_viewport_, viewport.scale, viewport.offset_x, viewport.offset_y, 0.0f);
    glUniform2f(u_surface_, static_cast<float>(surface_width),
                static_cast<float>(surface_height));
    glUseProgram(mesh_program_);
    glUniform4f(u_mesh_viewport_, viewport.scale, viewport.offset_x, viewport.offset_y, 0.0f);
    glUniform2f(u_mesh_surface_, static_cast<float>(surface_width),
                static_cast<float>(surface_height));
    glUseProgram(program_);
    for (std::uint32_t slot = 0; slot < kFontSlots; ++slot)
    {
        if (font_textures_[slot] == 0)
            continue;
        glActiveTexture(GL_TEXTURE1 + slot);
        glBindTexture(GL_TEXTURE_2D, font_textures_[slot]);
    }
    glActiveTexture(GL_TEXTURE0);

    GLuint bound_texture = 0;
    bool scissor = false;
    bool mesh_bound = true; // forces the batch bind on the first quad run
    bind_mesh_attribs();
    for (const Run &run : list.runs())
    {
        if (run.count == 0)
            continue;
        if (run.mesh != mesh_bound)
        {
            mesh_bound = run.mesh;
            glUseProgram(mesh_bound ? mesh_program_ : program_);
            if (mesh_bound)
                bind_mesh_attribs();
            else
                bind_quad_attribs();
        }
        if (run.texture != 0 && run.texture != bound_texture)
        {
            glBindTexture(GL_TEXTURE_2D, run.texture);
            bound_texture = run.texture;
        }
        if (run.clipped)
        {
            const float x0 = std::floor(run.clip.x * viewport.scale + viewport.offset_x);
            const float y0 = std::floor(run.clip.y * viewport.scale + viewport.offset_y);
            const float x1 =
                std::ceil((run.clip.x + run.clip.w) * viewport.scale + viewport.offset_x);
            const float y1 =
                std::ceil((run.clip.y + run.clip.h) * viewport.scale + viewport.offset_y);
            if (!scissor)
                glEnable(GL_SCISSOR_TEST);
            scissor = true;
            glScissor(static_cast<GLint>(x0), surface_height - static_cast<GLint>(y1),
                      static_cast<GLsizei>(std::max(0.0f, x1 - x0)),
                      static_cast<GLsizei>(std::max(0.0f, y1 - y0)));
        }
        else if (scissor)
        {
            glDisable(GL_SCISSOR_TEST);
            scissor = false;
        }
        if (run.mesh)
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first),
                         static_cast<GLsizei>(run.count));
        else
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first * 6),
                         static_cast<GLsizei>(run.count * 6));
        ++draw_calls_;
    }
    if (scissor)
        glDisable(GL_SCISSOR_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace hui::gfx
