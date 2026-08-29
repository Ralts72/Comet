#include "asset/metadata.h"
#include "core/project_paths.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace Comet::Tests {
    TEST(ProjectAssetsTest, DemoTexturesHaveStableMetadata) {
        const ProjectPaths paths(PROJECT_ROOT_DIR);
        const AssetMetadataSerializer serializer;

        const std::filesystem::path awesome_face_source =
                paths.assets() / "textures/awesomeface.png";
        ASSERT_TRUE(std::filesystem::exists(awesome_face_source));
        const AssetMetadata awesome_face = serializer.load(
            metadata_path(awesome_face_source));
        EXPECT_EQ(awesome_face.type, AssetType::Texture);
        EXPECT_EQ(
            awesome_face.handle,
            AssetHandle(15538271868700781231ull));

        const std::filesystem::path second_texture_source =
                paths.assets() / "textures/R-C.jpeg";
        ASSERT_TRUE(std::filesystem::exists(second_texture_source));
        const AssetMetadata second_texture = serializer.load(
            metadata_path(second_texture_source));
        EXPECT_EQ(second_texture.type, AssetType::Texture);
        EXPECT_EQ(
            second_texture.handle,
            AssetHandle(6692465245512631459ull));
    }
}
