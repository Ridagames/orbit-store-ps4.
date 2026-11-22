// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace hui::ps4 {
// ScePglConfig — the canonical layout flat_z recovered from the firmware's
// WebBrowserUIProcess.sprx; every PS4 GL homebrew (OpenOrbis, raylib,
// RetroArch) uses it verbatim. Piglet manages its own GPU heap when
// USE_FLEXIBLE_MEMORY is set, so no direct-memory allocation is needed.
struct ScePglConfig
{
    std::uint32_t size;                      // 0x00
    std::uint32_t flags;                     // 0x04
    std::uint8_t process_order;              // 0x08
    std::uint8_t reserved0[3];               // 0x09
    std::uint32_t reserved_0x0c;             // 0x0c
    std::uint32_t reserved_0x10;             // 0x10
    std::uint32_t reserved_0x14;             // 0x14
    std::uint64_t system_shared_memory_size; // 0x18
    std::uint32_t reserved_0x20;             // 0x20
    std::uint32_t reserved_0x24;             // 0x24
    std::uint64_t video_shared_memory_size;  // 0x28
    std::uint64_t max_mapped_flexible_memory;// 0x30
    std::uint64_t min_flexible_memory_chunk; // 0x38
    std::uint32_t dbg_pos_cmd_0x40;          // 0x40 — window x2 (width)
    std::uint32_t dbg_pos_cmd_0x44;          // 0x44 — window y2 (height)
    std::uint32_t dbg_pos_cmd_0x48;          // 0x48 — window x1
    std::uint32_t dbg_pos_cmd_0x4c;          // 0x4c — window y1
    std::uint8_t dbg_pos_cmd_0x50;           // 0x50
    std::uint8_t reserved_0x51[3];           // 0x51
    std::uint32_t draw_command_buffer_size;  // 0x54
    std::uint32_t lcue_resource_buffer_size; // 0x58
    std::uint32_t reserved_0x5c;             // 0x5c
    std::uint64_t reserved_0x60;             // 0x60
    std::uint64_t reserved_0x68;             // 0x68
    std::uint64_t reserved_0x70;             // 0x70
    std::uint64_t reserved_0x78;             // 0x78
    std::uint64_t reserved_0x80;             // 0x80
};
static_assert(sizeof(ScePglConfig) == 0x88);
static_assert(offsetof(ScePglConfig, system_shared_memory_size) == 0x18);
static_assert(offsetof(ScePglConfig, video_shared_memory_size) == 0x28);
static_assert(offsetof(ScePglConfig, draw_command_buffer_size) == 0x54);

// Named flag bits from the OpenOrbis headers. The canonical bring-up ORs
// in 0x60 on top — the documented tiled-texture bit (0x40) plus one
// undocumented bit (0x20) that every working sample sets.
constexpr std::uint32_t kPglUseCompositeExt = 0x08u;
constexpr std::uint32_t kPglUseTiledTexture = 0x40u;
constexpr std::uint32_t kPglUseFlexibleMemory = 0x80u;
constexpr std::uint32_t kPglFlags =
    kPglUseCompositeExt | kPglUseTiledTexture | kPglUseFlexibleMemory | 0x20u;

inline ScePglConfig piglet_configuration(int width, int height)
{
    ScePglConfig config{};
    config.size = sizeof(config);
    config.flags = kPglFlags;
    config.process_order = 1;
    config.system_shared_memory_size = 0x200000;
    config.video_shared_memory_size = 0x2400000;
    config.max_mapped_flexible_memory = 0xaa00000;
    config.min_flexible_memory_chunk = 0x10000;
    config.dbg_pos_cmd_0x40 = static_cast<std::uint32_t>(width);
    config.dbg_pos_cmd_0x44 = static_cast<std::uint32_t>(height);
    config.draw_command_buffer_size = 0xc0000;
    config.lcue_resource_buffer_size = 0x10000;
    config.reserved_0x5c = 2;
    return config;
}
} // namespace hui::ps4
