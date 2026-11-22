// Atlas paging preserves glyph metrics and pixels, including every bundled CJK glyph.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/save_file.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace hui::gfx {
namespace {
std::string utf8(std::uint32_t c) {
    std::string s;
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xc0 | c >> 6); s += char(0x80 | (c & 63)); }
    else if (c < 0x10000) { s += char(0xe0 | c >> 12); s += char(0x80 | ((c >> 6) & 63)); s += char(0x80 | (c & 63)); }
    else { s += char(0xf0 | c >> 18); s += char(0x80 | ((c >> 12) & 63)); s += char(0x80 | ((c >> 6) & 63)); s += char(0x80 | (c & 63)); }
    return s;
}
TEST(FontPages, AllBundledCjkGlyphsKeepTheirMetricsAndExactSdfPixels) {
    for (const char *name : {"jp", "kr", "sc", "tc"}) {
        SCOPED_TRACE(name);
        std::string packed; std::size_t size = 0;
        ASSERT_TRUE(save::read_file(std::string(HUI_SOURCE_DIR) + "/assets/fonts/noto-sans-" + name + ".huifontz", &packed, kFontFileLimit));
        auto data = inflate_font(packed, &size); ASSERT_NE(data, nullptr);
        Font original, paged;
        ASSERT_TRUE(original.load({data.get(), size}, false));
        ASSERT_TRUE(paged.load({data.get(), size}, false));
        ASSERT_TRUE(paged.paginate(4096)) << paged.error();
        ASSERT_GT(paged.page_count(), 1u);
        font_format::Header header; std::memcpy(&header, data.get(), sizeof header);
        for (std::size_t page = 0; page < paged.page_count(); ++page) {
            ASSERT_LE(paged.atlas_width(page), 4096); ASSERT_LE(paged.atlas_height(page), 4096);
        }
        for (std::size_t i = 0; i < header.glyph_count; ++i) {
            font_format::Glyph glyph;
            std::memcpy(&glyph, data.get() + sizeof header + i * sizeof glyph, sizeof glyph);
            SCOPED_TRACE(glyph.codepoint);
            // Single-character layout also handles Arabic presentation forms and combining glyphs.
            const auto text = utf8(glyph.codepoint);
            std::vector<GlyphQuad> before, after;
            EXPECT_EQ(original.layout(text, 3, 40, 32, Align::left, before),
                      paged.layout(text, 3, 40, 32, Align::left, after));
            ASSERT_EQ(before.size(), after.size());
            for (std::size_t q = 0; q < before.size(); ++q) {
                const auto &a = before[q], &b = after[q];
                EXPECT_EQ(a.x0, b.x0); EXPECT_EQ(a.x1, b.x1); EXPECT_EQ(a.y0, b.y0); EXPECT_EQ(a.y1, b.y1);
                const auto aw = original.atlas_width(), bw = paged.atlas_width(b.page);
                const int ax = std::lround(a.u0 * aw), ay = std::lround(a.v0 * original.atlas_height());
                const int bx = std::lround(b.u0 * bw), by = std::lround(b.v0 * paged.atlas_height(b.page));
                const int w = std::lround((a.u1 - a.u0) * aw), h = std::lround((a.v1 - a.v0) * original.atlas_height());
                ASSERT_LE(bx + w, bw); ASSERT_LE(by + h, paged.atlas_height(b.page));
                for (int row = 0; row < h; ++row)
                    ASSERT_EQ(std::memcmp(original.atlas_pixels() + (ay + row) * aw + ax,
                                          paged.atlas_pixels(b.page) + (by + row) * bw + bx, w), 0);
            }
        }
        paged.forget_atlas();
        EXPECT_EQ(paged.atlas_pixels(), nullptr);
        EXPECT_GT(paged.measure("日本語한국어中文", 32), 0);
    }
}
TEST(FontPages, DrawListBindsTheCorrectTextureForEachFallbackPage) {
    font_format::Header header{font_format::kMagic, font_format::kVersion, 16, 8, 8, 2, 6, -2, 0, 2, 0};
    font_format::Glyph glyphs[] = {{'A', 0, 0, 6, 6, 0, -6, 7}, {'B', 8, 0, 6, 6, 0, -6, 7}};
    std::string data(reinterpret_cast<char *>(&header), sizeof header);
    data.append(reinterpret_cast<char *>(glyphs), sizeof glyphs); data.append(128, '\x80');
    Font fallback; ASSERT_TRUE(fallback.load(data));
    ASSERT_TRUE(fallback.paginate(8)); ASSERT_EQ(fallback.page_count(), 2u);
    fallback.set_page_textures({101, 102});
    Font primary; header.glyph_count = 0;
    data.assign(reinterpret_cast<char *>(&header), sizeof header); data.append(128, '\0');
    ASSERT_TRUE(primary.load(data)); primary.set_fallback(&fallback, 101);
    DrawList list; list.text(primary, 99, "ABA", 0, 10, 8, {1, 1, 1, 1});
    ASSERT_EQ(list.runs().size(), 3u);
    EXPECT_EQ(list.runs()[0].texture, 101u); EXPECT_EQ(list.runs()[1].texture, 102u); EXPECT_EQ(list.runs()[2].texture, 101u);
    EXPECT_FALSE(fallback.paginate(4));
    EXPECT_EQ(fallback.measure("ABA", 8), 21);
}
}
}
