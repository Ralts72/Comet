#include "asset/database.h"
#include "asset/import/environment_importer.h"
#include "asset/import/mesh_importer.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <gtest/gtest.h>
#include <set>

namespace Comet::Tests {
    TEST(ProjectAssetsTest, DemoCubeNormalsPointOutwards) {
        const auto mesh = MeshImporter{}.import(
            ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY).assets() / "meshes/cube.gltf");
        ASSERT_TRUE(mesh) << mesh.error();
        ASSERT_FALSE(mesh.value().vertices.empty());
        for(const auto& vertex : mesh.value().vertices) {
            EXPECT_GT(Math::dot(vertex.position, vertex.normal), 0);
            EXPECT_NEAR(Math::length(vertex.normal), 1, 1e-5f);
        }
    }

    TEST(ProjectAssetsTest, DemoReferencesHaveUniqueMetadataAndValidMaterialDependencies) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        const auto root = project.value().paths().assets();
        std::set<AssetHandle> identities;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if(entry.path().extension() != ".meta")
                continue;
            const auto metadata = MetadataSerializer{}.load(entry.path());
            ASSERT_TRUE(metadata) << metadata.error();
            EXPECT_TRUE(metadata.value().handle);
            EXPECT_TRUE(identities.insert(metadata.value().handle).second);
        }
        AssetDatabase database(project.value().paths());
        ASSERT_TRUE(database.scan().snapshot_updated);
        const auto registry = create_scene_component_registry();
        const auto scene =
            SceneSerializer(registry).load((root / project.value().startup_scene()).string());
        ASSERT_TRUE(scene) << scene.error();
        for(const auto& reference : registry.collect_asset_references(*scene.value())) {
            EXPECT_TRUE(identities.contains(reference.handle));
            const auto* record = database.find(reference.handle);
            if(reference.required)
                ASSERT_NE(record, nullptr);
            if(record)
                EXPECT_EQ(record->type, reference.type);
        }
        for(const auto& record : database.get_assets()) {
            if(record.type != AssetType::Material)
                continue;
            const auto material = MaterialSerializer{}.load(root / record.path);
            ASSERT_TRUE(material) << material.error();
            EXPECT_FALSE(material.value().template_name.empty());
            for(const auto dependency : get_asset_dependencies(material.value())) {
                const auto* asset = database.find(dependency);
                ASSERT_NE(asset, nullptr);
                auto expected = AssetType::Texture;
                if(dependency == material.value().shader_program)
                    expected = AssetType::ShaderProgram;
                EXPECT_EQ(asset->type, expected);
            }
        }
    }

    class ProjectAssetIntegrationTest: public ::testing::TestWithParam<int> {};

    TEST_P(ProjectAssetIntegrationTest, DownloadedEnvironmentImportsAsCubemap) {
        const auto source =
            ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY).assets()
            / ("environments/small_hangar_01_" + std::to_string(GetParam()) + "k.hdr");
        if(!std::filesystem::exists(source))
            GTEST_SKIP() << "Optional HDR is absent; run ./tools/download_assets.sh";
        const auto imported = EnvironmentImporter{}.import(source);
        ASSERT_TRUE(imported) << imported.error();
        EXPECT_TRUE(imported.value().background.cubemap);
        EXPECT_EQ(imported.value().background.format, Format::R16G16B16A16_SFLOAT);
        EXPECT_EQ(imported.value().background.width, imported.value().background.height);
        EXPECT_GT(imported.value().background.mip_levels, 1u);
        EXPECT_FALSE(imported.value().background.pixels.empty());
    }

    INSTANTIATE_TEST_SUITE_P(
        Resolutions, ProjectAssetIntegrationTest, ::testing::Values(1, 2, 4, 8));
}
