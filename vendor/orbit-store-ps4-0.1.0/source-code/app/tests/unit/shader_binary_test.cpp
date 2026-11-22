#include "gfx/shader_binary.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>

TEST(ShaderBinary, RequiresExactSourceAndCompleteFormatHeader)
{
    char temp[] = "/tmp/orbit-shader-XXXXXX";
    ASSERT_NE(mkdtemp(temp), nullptr);
    const std::string base = std::string(temp) + "/example.vert";
    const std::string source = "#version 100\nvoid main() {}\n";
    auto write = [](const std::string &path, const std::string &bytes) {
        std::ofstream file(path, std::ios::binary); file.write(bytes.data(), bytes.size());
    };
    std::uint32_t format = 0; std::vector<std::uint8_t> bytes;
    EXPECT_FALSE(hui::gfx::read_shader_binary(base, source, &format, &bytes));
    write(base + ".glsl", source);
    write(base + ".bin", std::string("\x70\x92\0\0blob", 8));
    ASSERT_TRUE(hui::gfx::read_shader_binary(base, source, &format, &bytes));
    EXPECT_EQ(format, 0x9270u);
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "blob");
    EXPECT_FALSE(hui::gfx::read_shader_binary(base, source + "// changed", &format, &bytes));
    EXPECT_TRUE(bytes.empty());
    write(base + ".bin", std::string("\x70\x92\0\0", 4));
    EXPECT_FALSE(hui::gfx::read_shader_binary(base, source, &format, &bytes));
    unlink((base + ".glsl").c_str()); unlink((base + ".bin").c_str()); rmdir(temp);
}

TEST(ShaderBinary, CaptureRoundTripReplacementAndFailedPublication)
{
    char temp[] = "/tmp/orbit-shader-XXXXXX";
    ASSERT_NE(mkdtemp(temp), nullptr);
    const std::string base = std::string(temp) + "/capture.frag";
    const std::uint8_t first[] = {1, 2, 3}, next[] = {4, 5};
    std::uint32_t format; std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(hui::gfx::write_shader_binary(base, "first", 0, first, sizeof first));
    ASSERT_TRUE(hui::gfx::read_shader_binary(base, "first", &format, &bytes));
    EXPECT_EQ(format, 0u); EXPECT_EQ(bytes, (std::vector<std::uint8_t>{1, 2, 3}));
    ASSERT_TRUE(hui::gfx::write_shader_binary(base, "next", 0x9270, next, sizeof next));
    EXPECT_FALSE(hui::gfx::read_shader_binary(base, "first", &format, &bytes));
    ASSERT_TRUE(hui::gfx::read_shader_binary(base, "next", &format, &bytes));
    EXPECT_EQ(format, 0x9270u); EXPECT_EQ(bytes, (std::vector<std::uint8_t>{4, 5}));
    // Force binary publication to fail after invalidating the previous source.
    unlink((base + ".bin").c_str()); std::filesystem::create_directory(base + ".bin");
    EXPECT_FALSE(hui::gfx::write_shader_binary(base, "failed", 0, first, sizeof first));
    EXPECT_FALSE(std::filesystem::exists(base + ".glsl"));
    EXPECT_FALSE(hui::gfx::read_shader_binary(base, "next", &format, &bytes));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(temp),
                           std::filesystem::directory_iterator{}), 1);
    std::filesystem::remove_all(temp);
}
