#include "asset/import/texture_importer.h"
#include "asset/import/image_encoding.h"
#include <cstring>
#include <cmath>
#include "asset/data/texture_data.h"
#include "common/file_io.h"
#include "core/math_utils.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Comet::Tests {
    namespace {
        void write_rgba(const std::filesystem::path& path, const int width, const int height,
            const std::vector<std::array<std::uint8_t, 4>>& pixels) {
            std::array<std::uint8_t, 18> header{};
            header[2] = 2;
            header[12] = static_cast<std::uint8_t>(width);
            header[14] = static_cast<std::uint8_t>(height);
            header[16] = 32;
            header[17] = 0x28;
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(header.data()), header.size());
            for(const auto& rgba : pixels) {
                const std::array bgra{rgba[2], rgba[1], rgba[0], rgba[3]};
                output.write(reinterpret_cast<const char*>(bgra.data()), bgra.size());
            }
        }
    }

    TEST(ImageEncodingTest, PngUsesExposureAndSrgbWhileHdrPreservesLinearRadianceAndRowOrder) {
        const std::array<Math::Vec4, 4> radiance{Math::Vec4(4, 0, 0, 1),
            Math::Vec4(.5f, .5f, .5f, 1), Math::Vec4(0, 0, 2, 1), Math::Vec4(0, 0, 0, 1)};
        TextureData image{.width = 2,
            .height = 2,
            .format = Format::R32G32B32A32_SFLOAT,
            .pixels = std::vector<uint8_t>(sizeof(radiance))};
        std::memcpy(image.pixels.data(), radiance.data(), sizeof(radiance));
        const auto decode = [&](std::string_view extension, float exposure) {
            auto encoded = encode_image(image, extension, exposure);
            EXPECT_TRUE(encoded) << encoded.error();
            return TextureImporter{}.import(std::as_bytes(std::span(encoded.value())));
        };
        const auto hdr = encode_image(image, ".hdr", 2);
        ASSERT_TRUE(hdr);
        ASSERT_NE(hdr.value().find("-Y 2 +X 2"), std::string::npos);
        ASSERT_GE(hdr.value().size(), 16u);
        // 短扫描线使用逐像素 RGBE；二维 TextureImporter 的 8 位输出不适合验证 HDR 数值。
        const auto* rgbe =
            reinterpret_cast<const uint8_t*>(hdr.value().data() + hdr.value().size() - 16);
        for(std::size_t i = 0; i < radiance.size(); ++i) {
            const auto* pixel = rgbe + i * 4;
            const float scale = std::ldexp(1.f, int(pixel[3]) - 136);
            const Math::Vec3 linear{pixel[0] * scale, pixel[1] * scale, pixel[2] * scale};
            EXPECT_LE(glm::length(linear - Math::Vec3(radiance[i])), .02f);
        }
        auto dim = decode(".png", .5f);
        auto bright = decode(".png", 2);
        ASSERT_TRUE(dim);
        ASSERT_TRUE(bright);
        EXPECT_EQ(bright.value().pixels[0], 255);
        EXPECT_EQ(bright.value().pixels[1], 0);
        EXPECT_EQ(bright.value().pixels[2], 0);
        EXPECT_EQ(bright.value().pixels[8], 0);
        EXPECT_GT(bright.value().pixels[10], 250);
        EXPECT_GT(bright.value().pixels[4], dim.value().pixels[4]);
        EXPECT_EQ(bright.value().pixels[15], 255);
        auto black = decode(".png", 0);
        ASSERT_TRUE(black);
        EXPECT_EQ(black.value().pixels[0], 0);
        EXPECT_EQ(black.value().pixels[10], 0);
        EXPECT_FALSE(encode_image(image, ".jpg"));
        EXPECT_FALSE(encode_image(image, ".png", -1));
        image.pixels.resize(1);
        EXPECT_FALSE(encode_image(image, ".hdr"));
    }

    TEST(TextureImporterTest, DecodesTextureIntoRgbaPixels) {
        const std::filesystem::path source = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                             / "assets/textures/awesomeface.png";

        const TextureData data = TextureImporter{}.import(source).value();

        EXPECT_EQ(data.width, 512);
        EXPECT_EQ(data.height, 512);
        EXPECT_EQ(data.format, Format::R8G8B8A8_SRGB);
        EXPECT_EQ(data.mip_levels, 10u);
        EXPECT_EQ(data.pixels.size(), 1398100u);
    }

    TEST(TextureImporterTest, AppliesColorSpaceAndVerticalFlip) {
        const std::filesystem::path source = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                             / "assets/textures/awesomeface.png";
        const TextureImporter importer;
        const TextureData original = importer
                                         .import(source, {.color_space = TextureColorSpace::Linear,
                                                             .flip_y = false,
                                                             .generate_mipmaps = false})
                                         .value();
        const TextureData flipped = importer
                                        .import(source, {.color_space = TextureColorSpace::Linear,
                                                            .flip_y = true,
                                                            .generate_mipmaps = false})
                                        .value();

        EXPECT_EQ(original.format, Format::R8G8B8A8_UNORM);
        EXPECT_EQ(flipped.format, Format::R8G8B8A8_UNORM);
        ASSERT_EQ(original.pixels.size(), flipped.pixels.size());

        const std::size_t row_size = static_cast<std::size_t>(original.width) * 4;
        EXPECT_TRUE(std::equal(original.pixels.begin(),
            original.pixels.begin() + static_cast<std::ptrdiff_t>(row_size),
            flipped.pixels.end() - static_cast<std::ptrdiff_t>(row_size)));
        EXPECT_TRUE(std::equal(original.pixels.end() - static_cast<std::ptrdiff_t>(row_size),
            original.pixels.end(), flipped.pixels.begin()));
    }

    TEST(TextureImporterTest, EmbeddedBytesShareFileDecodingSettingsAndBudget) {
        const TemporaryDirectory directory;
        const auto source = directory.path() / "embedded.tga";
        write_rgba(source, 2, 2,
            {{218, 128, 218, 32}, {37, 128, 218, 96}, {128, 218, 218, 160}, {128, 37, 218, 224}});
        const auto encoded = read_binary_file(source, 1024);
        ASSERT_TRUE(encoded);
        const auto memory = std::span<const std::byte>(encoded.value());
        const auto estimate = TextureImporter::working_bytes(memory);
        ASSERT_TRUE(estimate);
        EXPECT_EQ(estimate.value(), TextureImporter::working_bytes(source).value());
        const TextureImporter importer;
        for(const bool normal_map : {false, true}) {
            const TextureImportSettings settings{.flip_y = true, .normal_map = normal_map};
            const auto file = importer.import(source, settings);
            const auto embedded = importer.import(memory, settings, estimate.value());
            ASSERT_TRUE(file) << file.error();
            ASSERT_TRUE(embedded) << embedded.error();
            EXPECT_EQ(embedded.value().pixels, file.value().pixels);
            EXPECT_EQ(embedded.value().mip_levels, 2u);
            EXPECT_EQ(embedded.value().format, file.value().format);
        }
        EXPECT_FALSE(importer.import(memory, {}, estimate.value() - 1));
        EXPECT_FALSE(importer.import(memory.first(10)));
        EXPECT_FALSE(importer.import(std::span<const std::byte>{}));
        AssetImportLimits limits;
        limits.source_bytes = memory.size() - 1;
        EXPECT_FALSE(importer.import(memory, {}, estimate.value(), limits));
    }

    TEST(TextureImporterTest, MipFilteringRespectsColorSpaceAndLinearAlpha) {
        const TemporaryDirectory directory;
        const auto source = directory.path() / "color.tga";
        write_rgba(
            source, 2, 2, {{0, 0, 0, 0}, {255, 255, 255, 255}, {0, 0, 0, 0}, {255, 255, 255, 255}});
        for(const auto color_space : {TextureColorSpace::Srgb, TextureColorSpace::Linear}) {
            const auto result = TextureImporter{}.import(source, {.color_space = color_space});
            ASSERT_TRUE(result) << result.error();
            const auto& data = result.value();
            ASSERT_EQ(data.pixels.size(), 20u);
            EXPECT_EQ(data.mip_levels, 2u);
            const int expected = color_space == TextureColorSpace::Srgb ? 188 : 128;
            for(unsigned channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(data.pixels[16 + channel], expected, 1);
            EXPECT_EQ(data.pixels[19], 128);
        }
        const auto base = TextureImporter{}.import(source, {.generate_mipmaps = false});
        ASSERT_TRUE(base);
        EXPECT_EQ(base.value().mip_levels, 1u);
        EXPECT_EQ(base.value().pixels.size(), 16u);
    }

    TEST(TextureImporterTest, NormalMapUsesLinearDataAndRenormalizesMips) {
        const TemporaryDirectory directory;
        const auto source = directory.path() / "normal.tga";
        write_rgba(source, 2, 1, {{218, 128, 218, 32}, {37, 128, 218, 96}});
        const auto result = TextureImporter{}.import(source, {.normal_map = true});
        ASSERT_TRUE(result) << result.error();
        const auto& data = result.value();
        EXPECT_EQ(data.format, Format::R8G8B8A8_UNORM);
        ASSERT_EQ(data.pixels.size(), 12u);
        EXPECT_EQ(data.pixels[0], 218);
        EXPECT_NEAR(data.pixels[8], 128, 1);
        EXPECT_NEAR(data.pixels[9], 128, 1);
        EXPECT_EQ(data.pixels[10], 255);
        EXPECT_EQ(data.pixels[11], 64);
        const auto linear =
            TextureImporter{}.import(source, {.color_space = TextureColorSpace::Linear});
        ASSERT_TRUE(linear);
        EXPECT_EQ(linear.value().pixels[10], 218);
    }

    TEST(TextureImporterTest, MipChainPreservesOddEdgesAndOneDimensionalTextures) {
        const TemporaryDirectory directory;
        const auto source = directory.path() / "odd.tga";
        for(const auto& [width, height] : {std::pair{3, 1}, {1, 3}, {3, 3}, {1, 1}}) {
            SCOPED_TRACE(std::to_string(width) + "x" + std::to_string(height));
            std::vector<std::array<std::uint8_t, 4>> pixels(width * height, {0, 0, 0, 255});
            pixels.back() = {255, 255, 255, 255};
            write_rgba(source, width, height, pixels);
            const auto result =
                TextureImporter{}.import(source, {.color_space = TextureColorSpace::Linear});
            ASSERT_TRUE(result) << result.error();
            const auto& data = result.value();
            EXPECT_EQ(data.mip_levels, width == 1 && height == 1 ? 1u : 2u);
            EXPECT_NEAR(data.pixels[data.pixels.size() - 4], 255.0 / (width * height), 1);
            EXPECT_EQ(data.pixels.back(), 255);
        }
    }

    TEST(TextureImporterTest, RespectsDecodedWorkingSetBudget) {
        const auto source = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                            / "assets/textures/awesomeface.png";
        const auto bytes = TextureImporter::working_bytes(source);
        ASSERT_TRUE(bytes);
        EXPECT_GT(bytes.value(), std::filesystem::file_size(source));
        EXPECT_FALSE(TextureImporter{}.import(source, {}, bytes.value() - 1));
        EXPECT_TRUE(TextureImporter{}.import(source, {}, bytes.value()));

        AssetImportLimits limits;
        limits.source_bytes = std::filesystem::file_size(source) - 1;
        EXPECT_FALSE(TextureImporter::working_bytes(source, limits));
        limits.source_bytes = AssetImportLimits{}.source_bytes;
        limits.texture_working_bytes = bytes.value() - 1;
        EXPECT_FALSE(TextureImporter::working_bytes(source, limits));
        EXPECT_FALSE(TextureImporter{}.import(source, {}, bytes.value(), limits));
    }

    TEST(TextureImporterTest, RejectsHugeDimensionsBeforeDecodingPixels) {
        const TemporaryDirectory directory;
        const auto source = directory.path() / "large-header.png";
        const auto sample = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                            / "assets/textures/awesomeface.png";
        std::ifstream input(sample, std::ios::binary);
        std::vector<char> encoded(std::istreambuf_iterator<char>(input), {});
        ASSERT_GT(encoded.size(), 24u);
        encoded[16] = encoded[20] = 0;
        encoded[17] = encoded[21] = 0;
        encoded[18] = encoded[22] = 0x40;
        encoded[19] = encoded[23] = 0;
        {
            std::ofstream output(source, std::ios::binary);
            output.write(encoded.data(), encoded.size());
        }

        const auto estimate = TextureImporter::working_bytes(source);
        ASSERT_FALSE(estimate);
        EXPECT_NE(estimate.error().find("working-set budget"), std::string::npos);
        EXPECT_FALSE(TextureImporter{}.import(source));
    }

    TEST(TextureImporterTest, RejectsInvalidImageData) {
        const TemporaryDirectory directory;
        const auto source = directory.path() / "invalid.png";
        {
            std::ofstream output(source, std::ios::binary);
            output << "not an image";
        }

        const auto result = TextureImporter{}.import(source);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find(source.string()), std::string::npos);
    }
}
