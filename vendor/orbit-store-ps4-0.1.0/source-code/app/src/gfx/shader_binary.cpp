#include "gfx/shader_binary.hpp"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace hui::gfx {
namespace {
bool read(const std::string &path, std::size_t limit, std::vector<std::uint8_t> *out) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? std::ftell(file) : -1;
    ok = length > 0 && static_cast<std::size_t>(length) <= limit && std::fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        out->resize(static_cast<std::size_t>(length));
        ok = std::fread(out->data(), 1, out->size(), file) == out->size();
    }
    std::fclose(file);
    if (!ok) out->clear();
    return ok;
}
}
bool read_shader_binary(const std::string &base, const std::string &source,
                        std::uint32_t *format, std::vector<std::uint8_t> *binary) {
    binary->clear(); *format = 0;
    std::vector<std::uint8_t> text, data;
    if (!read(base + ".glsl", 128 * 1024, &text) || text.size() != source.size() ||
        std::memcmp(text.data(), source.data(), source.size()) ||
        !read(base + ".bin", 1024 * 1024, &data) || data.size() <= 4) return false;
    *format = std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8) |
              (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
    binary->assign(data.begin() + 4, data.end());
    return true;
}
bool write_shader_binary(const std::string &base, const std::string &source,
                         std::uint32_t format, const std::uint8_t *binary, std::size_t size) {
    if (source.empty() || source.size() > 128 * 1024 || !binary || !size ||
        size > 1024 * 1024 - 4) return false;
    const auto slash = base.find_last_of('/');
    const std::string directory = slash == std::string::npos ? "." : slash == 0 ? "/" : base.substr(0, slash);
    const int parent = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent < 0) return false;
    std::string bin_tmp = base + ".bin.XXXXXX", src_tmp = base + ".glsl.XXXXXX";
    const auto save = [](std::string &name, const void *header, std::size_t header_size,
                         const void *bytes, std::size_t length) {
        int fd = mkstemp(name.data());
        if (fd < 0) return false;
        FILE *file = fdopen(fd, "wb");
        if (!file) { close(fd); unlink(name.c_str()); return false; }
        bool ok = (!header_size || std::fwrite(header, 1, header_size, file) == header_size) &&
            std::fwrite(bytes, 1, length, file) == length && !std::fflush(file) && !fsync(fd);
        if (std::fclose(file)) ok = false;
        if (!ok) unlink(name.c_str());
        return ok;
    };
    const std::uint8_t header[] = {std::uint8_t(format), std::uint8_t(format >> 8),
        std::uint8_t(format >> 16), std::uint8_t(format >> 24)};
    bool ok = save(bin_tmp, header, sizeof header, binary, size) &&
        save(src_tmp, nullptr, 0, source.data(), source.size());
    const std::string src = base + ".glsl", bin = base + ".bin";
    // Invalidate the old label before replacing its binary, including failures
    // between the two renames. Capture and packaged input directories differ.
    if (ok) ok = unlink(src.c_str()) == 0 || errno == ENOENT;
    if (ok) ok = fsync(parent) == 0;
    if (ok) ok = std::rename(bin_tmp.c_str(), bin.c_str()) == 0;
    if (ok) ok = std::rename(src_tmp.c_str(), src.c_str()) == 0;
    if (ok) ok = fsync(parent) == 0;
    unlink(bin_tmp.c_str()); unlink(src_tmp.c_str());
    close(parent);
    return ok;
}
}
