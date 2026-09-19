#include "asset/import/environment_importer.h"
#include "asset/database.h"
#include "support/hdr_image.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <glm/gtc/packing.hpp>
#include <cstring>

namespace Comet::Tests {
    static glm::vec4 pixel_at(const TextureData& data, size_t pixel) {
        glm::u16vec4 packed;
        std::memcpy(&packed, data.pixels.data() + pixel * sizeof(packed), sizeof(packed));
        return glm::unpackHalf(packed);
    }

    TEST(EnvironmentImporterTest, LightingConvolutionPreservesUniformRadianceAndIntegratesBrdf) {
        TemporaryDirectory directory;
        write_hdr(directory.path() / "uniform.hdr");
        auto imported = EnvironmentImporter{}.import(directory.path() / "uniform.hdr");
        ASSERT_TRUE(imported) << imported.error();
        const auto& data = imported.value();
        for(const auto* cube : {&data.irradiance, &data.specular}) {
            ASSERT_TRUE(cube->cubemap);
            for(size_t pixel = 0; pixel < cube->pixels.size() / 8; ++pixel)
                EXPECT_EQ(pixel_at(*cube, pixel), glm::vec4(4, 2, 1, 1));
        }
        ASSERT_FALSE(data.brdf.cubemap);
        for(size_t pixel = 0; pixel < data.brdf.pixels.size() / 8; ++pixel) {
            const auto value = pixel_at(data.brdf, pixel);
            EXPECT_TRUE(std::isfinite(value.x) && std::isfinite(value.y));
            EXPECT_GE(value.x, 0);
            EXPECT_GE(value.y, 0);
            EXPECT_LE(value.x + value.y, 1.03f);
        }
        const auto smooth = pixel_at(data.brdf, data.brdf.width - 1);
        EXPECT_NEAR(smooth.x + smooth.y, 1, 0.01f);
        const auto rough = pixel_at(data.brdf, size_t(data.brdf.width) * data.brdf.height - 1);
        // N=V、alpha=1 时，白色导体的积分结果为 1-ln(2)。
        EXPECT_NEAR(rough.x + rough.y, 1 - std::log(2.0), 0.015);
    }

    TEST(EnvironmentImporterTest, RoughnessPrefilterIsNotTheBackgroundMipChain) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "directional.hdr";
        write_hdr(path, 64, 32, [](int x, int) {
            unsigned char red = 0;
            if(x > 16 && x < 32)
                red = 128;
            return std::array<unsigned char, 4>{red, 0, 0, 129};
        });
        auto imported = EnvironmentImporter{}.import(path);
        ASSERT_TRUE(imported);
        const auto& data = imported.value();
        EXPECT_EQ(data.specular.mip_levels, data.background.mip_levels);
        EXPECT_NE(data.specular.pixels, data.background.pixels);
        float low_min = 1, low_max = 0, high_min = 1, high_max = 0;
        for(size_t i = 0; i < size_t(data.specular.width) * data.specular.height * 6; ++i) {
            const float value = pixel_at(data.specular, i).x;
            low_min = std::min(low_min, value);
            low_max = std::max(low_max, value);
        }
        const auto count = data.specular.pixels.size() / 8;
        for(size_t i = count - 6; i < count; ++i) {
            const float value = pixel_at(data.specular, i).x;
            high_min = std::min(high_min, value);
            high_max = std::max(high_max, value);
        }
        EXPECT_LT(high_max - high_min, low_max - low_min);
    }

    TEST(EnvironmentImporterTest, PreservesHdrEnergyInEveryFaceAndMip) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "studio.hdr";
        write_hdr(path);
        auto imported = EnvironmentImporter{}.import(path);
        ASSERT_TRUE(imported) << imported.error();
        const auto& data = imported.value().background;
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
            std::memcpy(
                &packed, imported.value().background.pixels.data() + face * 8, sizeof(packed));
            faces[face] = glm::unpackHalf(packed);
        }
        EXPECT_FLOAT_EQ(faces[0].x, 0.625f); // +X：中央经线。
        EXPECT_FLOAT_EQ(faces[1].x, 0.625f); // -X：跨接缝环绕采样。
        EXPECT_FLOAT_EQ(faces[2].y, 1.0f);   // +Y：顶部行。
        EXPECT_FLOAT_EQ(faces[3].y, 0.0f);   // -Y：底部行。
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
