#include "asset/texture_importer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

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
        EXPECT_EQ(data.format, Format::R8G8B8A8_UNORM);
        EXPECT_EQ(
            data.pixels.size(),
            static_cast<std::size_t>(data.width * data.height * data.channels));
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
