// Public PS4 browser-dialog parameter ABI.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PROVISIONAL: carried over from the PS5 dialog ABI. The PS4 WebBrowser
// dialog parameter layout has not been verified on console yet; every field
// offset is a P8 checklist item. The lifecycle calls themselves
// (Initialize/Open/UpdateStatus/GetResult/Close) are the documented PS4
// CommonDialog API.
#pragma once
#include <cstddef>
#include <cstdint>
namespace hui::ps4::dialog_abi {
struct Base { std::uint64_t size; std::uint8_t reserved[36]; std::uint32_t magic; };
struct Param {
    Base base;
    std::uint64_t size;
    std::int32_t mode, user;
    const char *url;
    void *callback;
    std::uint16_t width, height, x, y;
    std::uint32_t parts;
    std::uint16_t header_width, header_x, header_y, pad;
    std::uint32_t control;
    void *ime, *webview;
    std::uint32_t animation;
    std::uint8_t reserved[202];
    std::uint16_t tail;
};
static_assert(sizeof(Base) == 48 && sizeof(Param) == 328);
static_assert(offsetof(Param, url) == 64 && offsetof(Param, control) == 100);
struct Result { std::int32_t result, pad; void *callback; std::uint8_t reserved[240]; };
static_assert(sizeof(Result) == 256);
} // namespace hui::ps4::dialog_abi
