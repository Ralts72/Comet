#include "asset/import/texture_importer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace Comet::Tests {
    TEST(TextureImporterTest, DecodesTextureIntoRgbaPixels) {
        const std::filesystem::path source =
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
            / "assets/textures/awesomeface.png";

        const TextureData data = TextureImporter{}.import(source).value();

        EXPECT_EQ(data.width, 512);
        EXPECT_EQ(data.height, 512);
        EXPECT_EQ(data.format, Format::R8G8B8A8_SRGB);
        EXPECT_EQ(
            data.pixels.size(), static_cast<std::size_t>(data.width * data.height * 4));
    }

    TEST(TextureImporterTest, AppliesColorSpaceAndVerticalFlip) {
        const std::filesystem::path source =
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
            / "assets/textures/awesomeface.png";
        const TextureImporter importer;
        const TextureData original =
            importer
                .import(
                    source, {.color_space = TextureColorSpace::Linear, .flip_y = false})
                .value();
        const TextureData flipped =
            importer
                .import(
                    source, {.color_space = TextureColorSpace::Linear, .flip_y = true})
                .value();

        EXPECT_EQ(original.format, Format::R8G8B8A8_UNORM);
        EXPECT_EQ(flipped.format, Format::R8G8B8A8_UNORM);
        ASSERT_EQ(original.pixels.size(), flipped.pixels.size());

        const std::size_t row_size = static_cast<std::size_t>(original.width) * 4;
        EXPECT_TRUE(std::equal(original.pixels.begin(),
            original.pixels.begin() + static_cast<std::ptrdiff_t>(row_size),
            flipped.pixels.end() - static_cast<std::ptrdiff_t>(row_size)));
        EXPECT_TRUE(
            std::equal(original.pixels.end() - static_cast<std::ptrdiff_t>(row_size),
                original.pixels.end(), flipped.pixels.begin()));
    }

    TEST(TextureImporterTest, RejectsInvalidImageData) {
        const std::filesystem::path source =
            std::filesystem::temp_directory_path()
            / ("comet_invalid_texture_" + std::to_string(AssetHandle::generate().value())
                + ".png");
        {
            std::ofstream output(source, std::ios::binary);
            output << "not an image";
        }

        const auto result = TextureImporter{}.import(source);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find(source.string()), std::string::npos);

        std::error_code error;
        std::filesystem::remove(source, error);
    }
}
