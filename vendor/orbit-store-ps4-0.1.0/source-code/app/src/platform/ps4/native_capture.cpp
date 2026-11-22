// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/ps4/native_capture.hpp"
#ifdef ORBIT_NATIVE_CAPTURE
#include "platform/ps4/system.hpp"
#include "gfx/gl_headers.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hui::ps4 {
namespace {
constexpr const char *request_path = "/download0/orbit-store/native-capture.request";
bool write_all(int fd, const void *data, std::size_t size) {
    const auto *at = static_cast<const unsigned char *>(data);
    while (size) {
        const auto written = ::write(fd, at, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        at += written; size -= static_cast<std::size_t>(written);
    }
    return true;
}
}

void read_requested_input(InputFrame &input) {
    static std::int64_t next_check = 0;
    const auto now = sys::monotonic_us();
    if (now < next_check) return;
    next_check = now + 100000;
    constexpr const char *path = "/download0/orbit-store/native-input.request";
    const int fd = ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return;
    struct stat original{}, current{};
    char value[40]{};
    const bool valid = ::fstat(fd, &original) == 0 && S_ISREG(original.st_mode) &&
        original.st_nlink == 1 && original.st_size > 18 && original.st_size < 40 &&
        ::read(fd, value, sizeof(value)) == original.st_size;
    ::close(fd);
    if (!valid || value[16] != '\n' || value[original.st_size - 1] != '\n') return;
    for (int i = 0; i < 16; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return;
    value[original.st_size - 1] = 0;
    Action action = Action::count;
    Direction direction = Direction::none;
    const char *command = value + 17;
    const struct { const char *name; Action action; Direction direction; } allowed[] = {
        {"up", Action::up, Direction::up}, {"down", Action::down, Direction::down},
        {"left", Action::left, Direction::left}, {"right", Action::right, Direction::right},
        {"confirm", Action::confirm, Direction::none}, {"back", Action::back, Direction::none},
        {"page_prev", Action::page_prev, Direction::none}, {"page_next", Action::page_next, Direction::none},
        {"menu", Action::menu, Direction::none}, {"north", Action::north, Direction::none},
    };
    for (const auto &entry : allowed)
        if (std::strcmp(command, entry.name) == 0) { action = entry.action; direction = entry.direction; break; }
    if (action == Action::count) return;
    const int check = ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    const bool unchanged = check >= 0 && ::fstat(check, &current) == 0 &&
        current.st_ino == original.st_ino && current.st_dev == original.st_dev;
    if (check >= 0) ::close(check);
    if (!unchanged || ::unlink(path) != 0) return;
    input = {};
    input.connected = true;
    input.pressed = action_bit(action);
    input.nav = direction;
    value[16] = 0;
    char receipt[160];
    std::snprintf(receipt, sizeof(receipt), "/download0/orbit-store/native-input-%s.done", value);
    const int out = ::open(receipt, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (out >= 0) { write_all(out, command, std::strlen(command)); ::close(out); }
    sys::log("[ORBIT] native test input %s", command);
}

void capture_requested_frame(int width, int height) {
    static std::int64_t next_check = 0;
    static bool announced = false;
    if (!announced) { sys::log("[ORBIT] native capture enabled"); announced = true; }
    const auto now = sys::monotonic_us();
    if (now < next_check) return;
    next_check = now + 500000;
    const int request = ::open(request_path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (request < 0) {
        static int last_error = 0;
        if (errno != ENOENT && errno != last_error) {
            last_error = errno;
            sys::log("[ORBIT] native capture request open errno=%d", errno);
        }
        return;
    }
    struct stat original{}, current{};
    char nonce[18]{};
    const int stat_result = ::fstat(request, &original);
    const auto read_result = stat_result == 0 ? ::read(request, nonce, 18) : -1;
    bool valid = stat_result == 0 && S_ISREG(original.st_mode) &&
        original.st_nlink == 1 && original.st_size == 17 && read_result == 17 &&
        nonce[16] == '\n';
    ::close(request);
    for (int i = 0; valid && i < 16; ++i)
        valid = (nonce[i] >= '0' && nonce[i] <= '9') || (nonce[i] >= 'a' && nonce[i] <= 'f');
    // The pinned PS4 libc's lstat/fstatat are ENOSYS stubs. A second no-follow
    // descriptor still verifies the current path without accepting a symlink.
    const int current_fd = ::open(request_path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    const int path_result = current_fd < 0 ? -1 : ::fstat(current_fd, &current);
    if (current_fd >= 0) ::close(current_fd);
    if (!valid || path_result != 0 || current.st_dev != original.st_dev ||
        current.st_ino != original.st_ino) {
        static bool reported = false;
        if (!reported) {
            sys::log("[ORBIT] native capture invalid request stat=%d mode=%x links=%d size=%lld read=%lld path=%d same=%d",
                stat_result, static_cast<unsigned>(original.st_mode), static_cast<int>(original.st_nlink),
                static_cast<long long>(original.st_size), static_cast<long long>(read_result), path_result,
                current.st_dev == original.st_dev && current.st_ino == original.st_ino);
            reported = true;
        }
        return;
    }
    if (::unlink(request_path) != 0) { sys::log("[ORBIT] native capture unlink errno=%d", errno); return; }
    nonce[16] = 0;
    if (width < 1 || height < 1 || width > 1920 || height > 1080) {
        sys::log("[ORBIT] native capture rejected dimensions %dx%d", width, height); return;
    }
    const auto bytes = static_cast<std::size_t>(width) * height * 4;
    std::unique_ptr<unsigned char[]> pixels(new (std::nothrow) unsigned char[bytes]);
    if (!pixels) { sys::log("[ORBIT] native capture allocation failed"); return; }
    GLint previous_pack = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    const GLenum error = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, previous_pack);
    if (error != GL_NO_ERROR) {
        sys::log("[ORBIT] native capture read failed 0x%x", static_cast<unsigned>(error)); return;
    }
    struct Header { char magic[8]; std::uint32_t width, height; char nonce[16]; } header{};
    static_assert(sizeof(header) == 32);
    std::memcpy(header.magic, "ORBCAP01", 8);
    header.width = width; header.height = height;
    std::memcpy(header.nonce, nonce, 16);
    char path[160];
    std::snprintf(path, sizeof path, "/download0/orbit-store/native-capture-%s.rgba", nonce);
    const int out = ::open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (out < 0) { sys::log("[ORBIT] native capture file could not open"); return; }
    bool ok = write_all(out, &header, sizeof header) && write_all(out, pixels.get(), bytes);
    if (::fsync(out) != 0) ok = false;
    if (::close(out) != 0) ok = false;
    if (!ok) ::unlink(path);
    sys::log("[ORBIT] native capture %s %dx%d id=%s", ok ? "saved" : "failed", width, height, nonce);
}
}
#endif
