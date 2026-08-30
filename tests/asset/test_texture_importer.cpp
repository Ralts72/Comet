#include "asset/import/texture_importer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Comet::Tests {
    TEST(TextureImporterTest, DecodesTextureIntoRgbaPixels) {
        const std::filesystem::path source =
                std::filesystem::path(PROJECT_ROOT_DIR)
                / "assets/textures/awesomeface.png";

        const TextureData data = TextureImporter{}.import(source);

        EXPECT_EQ(data.width, 512);
        EXPECT_EQ(data.height, 512);
        EXPECT_EQ(data.channels, 4);
        EXPECT_EQ(data.format, Format::R8G8B8A8_SRGB);
        EXPECT_EQ(
            data.pixels.size(),
            static_cast<std::size_t>(data.width * data.height * data.channels));
    }

    TEST(TextureImporterTest, AppliesColorSpaceAndVerticalFlip) {
        const std::filesystem::path source =
                std::filesystem::path(PROJECT_ROOT_DIR)
                / "assets/textures/awesomeface.png";
        const TextureImporter importer;
        const TextureData original = importer.import(source, {
            .color_space = TextureColorSpace::Linear,
            .flip_y = false
        });
        const TextureData flipped = importer.import(source, {
            .color_space = TextureColorSpace::Linear,
            .flip_y = true
        });

        EXPECT_EQ(original.format, Format::R8G8B8A8_UNORM);
        EXPECT_EQ(flipped.format, Format::R8G8B8A8_UNORM);
        ASSERT_EQ(original.pixels.size(), flipped.pixels.size());

        const std::size_t row_size = static_cast<std::size_t>(original.width)
            * original.channels;
        EXPECT_TRUE(std::equal(
            original.pixels.begin(),
            original.pixels.begin() + static_cast<std::ptrdiff_t>(row_size),
            flipped.pixels.end() - static_cast<std::ptrdiff_t>(row_size)));
        EXPECT_TRUE(std::equal(
            original.pixels.end() - static_cast<std::ptrdiff_t>(row_size),
            original.pixels.end(),
            flipped.pixels.begin()));
    }

    TEST(TextureImporterTest, RejectsInvalidImageData) {
        const std::filesystem::path source =
                std::filesystem::temp_directory_path()
                / ("comet_invalid_texture_"
                   + std::to_string(AssetHandle::generate().value()) + ".png");
        {
            std::ofstream output(source, std::ios::binary);
            output << "not an image";
        }

        EXPECT_THROW(
            static_cast<void>(TextureImporter{}.import(source)),
            std::runtime_error);

        std::error_code error;
        std::filesystem::remove(source, error);
    }
}
