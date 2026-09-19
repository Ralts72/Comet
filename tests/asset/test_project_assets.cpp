#include "asset/metadata.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/project_paths.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace Comet::Tests {
    TEST(ProjectAssetsTest, DemoAssetsHaveStableIdentityAndValidReferences) {
        const ProjectPaths paths(COMET_SAMPLE_PROJECT_DIRECTORY);
        const MetadataSerializer serializer;

        const std::filesystem::path awesome_face_source =
            paths.assets() / "textures/awesomeface.png";
        ASSERT_TRUE(std::filesystem::exists(awesome_face_source));
        const AssetMetadata awesome_face =
            serializer.load(metadata_path(awesome_face_source)).value();
        EXPECT_EQ(awesome_face.type, AssetType::Texture);
        EXPECT_EQ(awesome_face.handle, AssetHandle(15538271868700781231ull));
        EXPECT_NE(std::get_if<TextureImportSettings>(&awesome_face.import_settings), nullptr);

        const std::filesystem::path second_texture_source = paths.assets() / "textures/R-C.jpeg";
        ASSERT_TRUE(std::filesystem::exists(second_texture_source));
        const AssetMetadata second_texture =
            serializer.load(metadata_path(second_texture_source)).value();
        EXPECT_EQ(second_texture.type, AssetType::Texture);
        EXPECT_EQ(second_texture.handle, AssetHandle(6692465245512631459ull));
        EXPECT_NE(std::get_if<TextureImportSettings>(&second_texture.import_settings), nullptr);

        const auto solid_path = paths.assets() / "materials/solid.mat";
        const auto solid_meta = serializer.load(metadata_path(solid_path));
        ASSERT_TRUE(solid_meta) << solid_meta.error();
        EXPECT_EQ(solid_meta.value().type, AssetType::Material);
        const auto solid_data = MaterialSerializer{}.load(solid_path);
        ASSERT_TRUE(solid_data) << solid_data.error();
        EXPECT_EQ(solid_data.value().template_name, "unlit_color");
        EXPECT_TRUE(get_asset_dependencies(solid_data.value()).empty());
        const auto pbr_path = paths.assets() / "materials/pbr.mat";
        const auto pbr_meta = serializer.load(metadata_path(pbr_path));
        ASSERT_TRUE(pbr_meta) << pbr_meta.error();
        EXPECT_EQ(pbr_meta.value().type, AssetType::Material);
        EXPECT_EQ(pbr_meta.value().handle, AssetHandle(6482638524486200214ull));
        EXPECT_NE(pbr_meta.value().handle, solid_meta.value().handle);
        const auto pbr = MaterialSerializer{}.load(pbr_path);
        ASSERT_TRUE(pbr) << pbr.error();
        EXPECT_EQ(pbr.value().template_name, "pbr");
        EXPECT_EQ(pbr.value().texture_properties.at("base_color_texture"), awesome_face.handle);
        EXPECT_EQ(
            get_asset_dependencies(pbr.value()), std::vector<AssetHandle>{awesome_face.handle});
        const auto ground_path = paths.assets() / "materials/ground.mat";
        const auto ground_meta = serializer.load(metadata_path(ground_path));
        ASSERT_TRUE(ground_meta) << ground_meta.error();
        EXPECT_EQ(ground_meta.value().handle, AssetHandle(9429603817048603172ull));
        const auto ground = MaterialSerializer{}.load(ground_path);
        ASSERT_TRUE(ground) << ground.error();
        EXPECT_EQ(ground.value().template_name, "pbr");
        EXPECT_TRUE(get_asset_dependencies(ground.value()).empty());
        EXPECT_FLOAT_EQ(ground.value().scalar_properties.at("metallic"), 0);
        EXPECT_FLOAT_EQ(ground.value().scalar_properties.at("roughness"), 1);
    }
}
