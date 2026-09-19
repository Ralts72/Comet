#include "asset/import/environment_importer.h"
#include "asset/database.h"
#include "support/hdr_image.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <glm/gtc/packing.hpp>
#include <cstring>

namespace Comet::Tests {
    TEST(EnvironmentImporterTest, PreservesHdrEnergyInEveryFaceAndMip) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "studio.hdr";
        write_hdr(path);
        auto imported = EnvironmentImporter{}.import(path);
        ASSERT_TRUE(imported) << imported.error();
        const auto& data = imported.value();
        EXPECT_TRUE(data.cubemap);
        EXPECT_EQ(data.format, Format::R16G16B16A16_SFLOAT);
        EXPECT_EQ(data.width, 4);
        EXPECT_EQ(data.height, 4);
        EXPECT_EQ(data.mip_levels, 3u);
        ASSERT_EQ(data.pixels.size(), (16 + 4 + 1) * 6 * 8u);
        for(size_t offset = 0; offset < data.pixels.size(); offset += 8) {
            glm::u16vec4 packed;
            std::memcpy(&packed, data.pixels.data() + offset, sizeof(packed));
            const auto pixel = glm::unpackHalf(packed);
            EXPECT_EQ(pixel, glm::vec4(4, 2, 1, 1));
        }
    }

    TEST(EnvironmentImporterTest, CubemapUsesYUpAndWrapsLongitude) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "axes.hdr";
        write_hdr(path, 4, 2, [](int x, int y) {
            return std::array<unsigned char, 4>{static_cast<unsigned char>(32 * (x + 1)),
                static_cast<unsigned char>(y == 0 ? 128 : 0), 0, 129};
        });
        auto imported = EnvironmentImporter{}.import(path);
        ASSERT_TRUE(imported) << imported.error();
        std::array<glm::vec4, 6> faces;
        for(size_t face = 0; face < 6; ++face) {
            glm::u16vec4 packed;
            std::memcpy(&packed, imported.value().pixels.data() + face * 8, sizeof(packed));
            faces[face] = glm::unpackHalf(packed);
        }
        EXPECT_FLOAT_EQ(faces[0].x, 0.625f); // +X: center longitude
        EXPECT_FLOAT_EQ(faces[1].x, 0.625f); // -X: wrapped seam
        EXPECT_FLOAT_EQ(faces[2].y, 1.0f);   // +Y: top row
        EXPECT_FLOAT_EQ(faces[3].y, 0.0f);   // -Y: bottom row
        EXPECT_FLOAT_EQ(faces[4].x, 0.875f); // +Z
        EXPECT_FLOAT_EQ(faces[5].x, 0.375f); // -Z
    }

    TEST(EnvironmentImporterTest, RejectsWrongShapeTruncatedAndOutOfRangeInputs) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "invalid.hdr";
        write_hdr(path, 4, 4);
        EXPECT_FALSE(EnvironmentImporter{}.import(path));
        std::ofstream(path) << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
        EXPECT_FALSE(EnvironmentImporter{}.import(path));
        write_hdr(
            path, 4, 2, [](int, int) { return std::array<unsigned char, 4>{255, 0, 0, 160}; });
        EXPECT_FALSE(EnvironmentImporter{}.import(path));
        EXPECT_FALSE(EnvironmentImporter{}.import(directory.path() / "missing.hdr"));
        EXPECT_FALSE(
            EnvironmentImporter{}.import(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                         / "assets/textures/awesomeface.png"));
    }

    TEST(EnvironmentImporterTest, DatabaseSeparatesEnvironmentsFromMaterialTextures) {
        TemporaryDirectory directory;
        ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        write_hdr(paths.assets() / "studio.HDR");
        AssetDatabase database(paths);
        static_cast<void>(database.scan());
        const auto* record = database.find("studio.HDR");
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->type, AssetType::Environment);
        EXPECT_EQ(to_string(record->type), "environment");
        EXPECT_TRUE(std::holds_alternative<std::monostate>(record->import_settings));
        EXPECT_EQ(asset_type_from_string("environment"), AssetType::Environment);
    }

    TEST(EnvironmentImporterTest, ChecksCompleteRleRunsIncludingLastPixel) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "rle.hdr";
        std::string data = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
        const size_t header_size = data.size();
        for(int row = 0; row < 4; ++row) {
            for(const unsigned char value : {2, 2, 0, 8, 136, 128, 136, 64, 136, 32, 136, 131})
                data.push_back(static_cast<char>(value));
        }
        {
            std::ofstream output(path, std::ios::binary);
            output.write(data.data(), data.size());
        }
        ASSERT_TRUE(EnvironmentImporter{}.import(path));
        for(size_t end = header_size; end < data.size(); ++end) {
            {
                std::ofstream output(path, std::ios::binary);
                output.write(data.data(), end);
            }
            EXPECT_FALSE(EnvironmentImporter{}.import(path)) << end;
        }
    }
}
