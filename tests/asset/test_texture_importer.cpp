#include "asset/import/texture_importer.h"
#include "asset/data/texture_data.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Comet::Tests {
    TEST(TextureImporterTest, DecodesTextureIntoRgbaPixels) {
        const std::filesystem::path source = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                             / "assets/textures/awesomeface.png";

        const TextureData data = TextureImporter{}.import(source).value();

        EXPECT_EQ(data.width, 512);
        EXPECT_EQ(data.height, 512);
        EXPECT_EQ(data.format, Format::R8G8B8A8_SRGB);
        EXPECT_EQ(data.pixels.size(), static_cast<std::size_t>(data.width * data.height * 4));
    }

    TEST(TextureImporterTest, AppliesColorSpaceAndVerticalFlip) {
        const std::filesystem::path source = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                             / "assets/textures/awesomeface.png";
        const TextureImporter importer;
        const TextureData original =
            importer.import(source, {.color_space = TextureColorSpace::Linear, .flip_y = false})
                .value();
        const TextureData flipped =
            importer.import(source, {.color_space = TextureColorSpace::Linear, .flip_y = true})
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
        const auto source =
            std::filesystem::temp_directory_path()
            / ("comet_large_header_" + std::to_string(AssetHandle::generate().value()) + ".png");
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

        std::error_code error;
        std::filesystem::remove(source, error);
    }

    TEST(TextureImporterTest, RejectsInvalidImageData) {
        const std::filesystem::path source =
            std::filesystem::temp_directory_path()
            / ("comet_invalid_texture_" + std::to_string(AssetHandle::generate().value()) + ".png");
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
