// Orbit Store TV app - Textures in OpenGL, for the console and the PC preview.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/gl_batch.hpp"
#include "orbit/art.hpp"

#include "gfx/gl_headers.hpp"
#ifdef ORBIT_PERF_TRACE
#include "platform/ps4/system.hpp"
#include <map>
#endif

namespace orbit
{

class GlTextures final : public TextureSink
{
  public:
    explicit GlTextures(hui::gfx::GlBatch &batch) : batch_(batch)
    {
    }
    std::uint32_t create(const image::Pixels &pixels) override
    {
        if (pixels.empty())
            return 0;
#ifdef ORBIT_PERF_TRACE
        // Test-only: texture memory in use, and uploads the GPU refused.
        const std::uint32_t made = batch_.create_texture(pixels.width, pixels.height, pixels.rgba.data());
        const std::size_t bytes = static_cast<std::size_t>(pixels.width) * pixels.height * 4;
        if (made)
        {
            live_[made] = bytes;
            live_bytes_ += bytes;
        }
        else
            hui::sys::log("[PERF] texture failed %dx%d live_mb=%zu", pixels.width, pixels.height, live_bytes_ >> 20);
        return made;
#else
        return batch_.create_texture(pixels.width, pixels.height, pixels.rgba.data());
#endif
    }
    void destroy(std::uint32_t texture) override
    {
        const GLuint name = texture;
        if (name != 0)
            glDeleteTextures(1, &name);
#ifdef ORBIT_PERF_TRACE
        const auto found = live_.find(texture);
        if (found != live_.end())
        {
            live_bytes_ -= found->second;
            live_.erase(found);
        }
#endif
    }
#ifdef ORBIT_PERF_TRACE
    std::size_t live_megabytes() const
    {
        return live_bytes_ >> 20;
    }
#endif

  private:
    hui::gfx::GlBatch &batch_;
#ifdef ORBIT_PERF_TRACE
    std::map<std::uint32_t, std::size_t> live_;
    std::size_t live_bytes_ = 0;
#endif
};

} // namespace orbit
