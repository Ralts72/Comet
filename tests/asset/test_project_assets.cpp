#include "asset/metadata.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/project_paths.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace Comet::Tests {
    TEST(ProjectAssetsTest, DemoAssetsHaveStableIdentityAndValidReferences) {
        const ProjectPaths paths(PROJECT_ROOT_DIR);
        const AssetMetadataSerializer serializer;

        const std::filesystem::path awesome_face_source =
            paths.assets() / "textures/awesomeface.png";
        ASSERT_TRUE(std::filesystem::exists(awesome_face_source));
        const AssetMetadata awesome_face =
            serializer.load(metadata_path(awesome_face_source));
        EXPECT_EQ(awesome_face.type, AssetType::Texture);
        EXPECT_EQ(awesome_face.handle, AssetHandle(15538271868700781231ull));
        EXPECT_NE(
            std::get_if<TextureImportSettings>(&awesome_face.import_settings), nullptr);

        const std::filesystem::path second_texture_source =
            paths.assets() / "textures/R-C.jpeg";
        ASSERT_TRUE(std::filesystem::exists(second_texture_source));
        const AssetMetadata second_texture =
            serializer.load(metadata_path(second_texture_source));
        EXPECT_EQ(second_texture.type, AssetType::Texture);
        EXPECT_EQ(second_texture.handle, AssetHandle(6692465245512631459ull));
        EXPECT_NE(
            std::get_if<TextureImportSettings>(&second_texture.import_settings), nullptr);

        const std::filesystem::path material_source =
            paths.assets() / "materials/demo.mat";
        ASSERT_TRUE(std::filesystem::exists(material_source));
        const AssetMetadata material = serializer.load(metadata_path(material_source));
        EXPECT_EQ(material.type, AssetType::Material);
        EXPECT_EQ(material.handle, AssetHandle(11364856686536078871ull));

        const MaterialData material_data = MaterialSerializer{}.load(material_source);
        EXPECT_EQ(material_data.template_name, "cube_texture");
        EXPECT_EQ(material_data.texture_properties.at("u_Texture0"), awesome_face.handle);
        EXPECT_EQ(
            material_data.texture_properties.at("u_Texture1"), second_texture.handle);
    }
}
