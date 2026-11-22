// ps4-homebrew-ui - DualShock 4 input through scePad.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps4/pad.hpp"

#include "platform/ps4/system.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

// scePadRead sample layout on PS4 (ScePadData). Only the leading analog
// fields and the connected/timestamp offsets are read; the motion and touch
// blocks keep their documented place. Layout follows the OpenOrbis headers.
struct RawPadSample
{
    std::uint32_t buttons;
    std::uint8_t left_x;
    std::uint8_t left_y;
    std::uint8_t right_x;
    std::uint8_t right_y;
    std::uint8_t l2;
    std::uint8_t r2;
    std::uint8_t reserved0[2];
    float orientation[4];
    float acceleration[3];
    float angular_velocity[3];
    std::uint8_t touch_data[24];
    std::uint8_t connected;
    std::uint8_t reserved1[3];
    std::uint64_t timestamp_us;
    std::uint8_t extension[16];
    std::uint8_t connected_count;
    std::uint8_t reserved2[2];
    std::uint8_t device_unique_data_len;
    std::uint8_t device_unique_data[12];
};
static_assert(offsetof(RawPadSample, left_x) == 4);
static_assert(offsetof(RawPadSample, r2) == 9);
static_assert(offsetof(RawPadSample, connected) == 76);
static_assert(offsetof(RawPadSample, timestamp_us) == 80);
static_assert(offsetof(RawPadSample, connected_count) == 104);
static_assert(sizeof(RawPadSample) == 120);

constexpr int kAlreadyInitialized = static_cast<int>(0x80960003);

struct PadVibration
{
    std::uint8_t large_motor;
    std::uint8_t small_motor;
};

struct PadColor
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t reserved;
};

} // namespace

extern "C"
{
    int sceUserServiceInitialize(const void *params);
    int sceUserServiceTerminate(void);
    int sceUserServiceGetInitialUser(int *user);
    int scePadInit(void);
    int scePadOpen(int user, int type, int index, const void *parameters);
    int scePadRead(int handle, RawPadSample *samples, int count);
    int scePadClose(int handle);
    int scePadSetVibration(int handle, const PadVibration *vibration);
    int scePadSetLightBar(int handle, const PadColor *color);
}

namespace hui::ps4
{

Pad::~Pad()
{
    close();
}

bool Pad::open()
{
    const int init = sceUserServiceInitialize(nullptr);
    owns_user_service_ = init == 0;
    if (init != 0 && init != kAlreadyInitialized)
        sys::log("[HUI] pad sceUserServiceInitialize=0x%08x", static_cast<unsigned>(init));
    if (sceUserServiceGetInitialUser(&user_) != 0)
    {
        sys::log("[HUI] pad no initial user");
        return false;
    }
    const int pad_init = scePadInit();
    if (pad_init != 0)
        sys::log("[HUI] pad scePadInit=0x%08x", static_cast<unsigned>(pad_init));
    // The pad service can lag behind title start; retry briefly.
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        handle_ = scePadOpen(user_, 0, 0, nullptr);
        if (handle_ >= 0)
        {
            sys::log("[HUI] pad open user=0x%08x handle=%d attempts=%d",
                     static_cast<unsigned>(user_), handle_, attempt + 1);
            return true;
        }
        sys::sleep_us(50000);
    }
    sys::log("[HUI] pad scePadOpen failed=0x%08x", static_cast<unsigned>(handle_));
    handle_ = -1;
    return false;
}

void Pad::set_light_bar(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    const std::uint32_t packed =
        (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
    if (handle_ < 0 || packed == light_bar_)
        return;
    light_bar_ = packed;
    const PadColor color{r, g, b, 0};
    const int result = scePadSetLightBar(handle_, &color);
    if (result != 0)
        sys::log("[HUI] pad scePadSetLightBar=0x%08x", static_cast<unsigned>(result));
}

void Pad::rumble(float strength, float seconds)
{
    if (handle_ < 0 || strength <= 0.0f || seconds <= 0.0f)
        return;
    const float clamped = strength > 1.0f ? 1.0f : strength;
    // The small motor gives the crisp tick a UI wants; the large one adds
    // weight only to strong effects.
    const PadVibration vibration{
        static_cast<std::uint8_t>(clamped > 0.6f ? (clamped - 0.6f) * 2.5f * 255.0f : 0.0f),
        static_cast<std::uint8_t>(clamped * 255.0f)};
    scePadSetVibration(handle_, &vibration);
    rumble_left_ = seconds;
}

void Pad::tick(float dt)
{
    if (rumble_left_ <= 0.0f)
        return;
    rumble_left_ -= dt;
    if (rumble_left_ <= 0.0f && handle_ >= 0)
    {
        const PadVibration stop{0, 0};
        scePadSetVibration(handle_, &stop);
    }
}

void Pad::close()
{
    if (handle_ >= 0)
    {
        const PadVibration stop{0, 0};
        scePadSetVibration(handle_, &stop);
        scePadClose(handle_);
    }
    handle_ = -1;
    if (owns_user_service_)
        sceUserServiceTerminate();
    owns_user_service_ = false;
}

std::size_t Pad::read(std::span<PadSample> out)
{
    if (out.empty())
        return 0;
    if (handle_ < 0)
    {
        out[0] = PadSample{};
        return 1;
    }
    RawPadSample raw[64];
    const int limit = static_cast<int>(out.size() < 64 ? out.size() : 64);
    const int count = scePadRead(handle_, raw, limit);
    if (count <= 0)
        return 0;
    for (int index = 0; index < count; ++index)
    {
        const RawPadSample &r = raw[index];
        PadSample &s = out[static_cast<std::size_t>(index)];
        s.buttons = r.buttons;
        s.left_x = r.left_x;
        s.left_y = r.left_y;
        s.right_x = r.right_x;
        s.right_y = r.right_y;
        s.l2 = r.l2;
        s.r2 = r.r2;
        s.connected = r.connected != 0;
        s.timestamp_us = r.timestamp_us;
    }
    return static_cast<std::size_t>(count);
}

} // namespace hui::ps4
