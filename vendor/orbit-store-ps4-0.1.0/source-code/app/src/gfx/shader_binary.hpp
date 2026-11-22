#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hui::gfx {
// A capture is a little-endian GL format followed by Piglet's shader blob.
// Its adjacent GLSL must exactly match the current program, including its prefix.
bool read_shader_binary(const std::string &base, const std::string &source,
                        std::uint32_t *format, std::vector<std::uint8_t> *binary);
// Writes complete temporary files, then publishes the source last. A failed
// capture cannot leave a new binary labelled with an older source.
bool write_shader_binary(const std::string &base, const std::string &source,
                         std::uint32_t format, const std::uint8_t *binary, std::size_t size);
}
