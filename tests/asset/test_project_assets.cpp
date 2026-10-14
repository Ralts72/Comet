#include "asset/default_assets.h"
#include "asset/database.h"
#include "asset/import/environment_importer.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/model_importer.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <gtest/gtest.h>
#include <set>
#include <map>

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
            for(const auto& child : metadata.value().subresources) {
                EXPECT_TRUE(child.handle);
                EXPECT_TRUE(identities.insert(child.handle).second);
            }
        }
        AssetDatabase database(project.value().paths());
        const auto scan = database.scan();
        for(const auto& issue : scan.issues)
            ADD_FAILURE() << issue.message;
        ASSERT_TRUE(scan.succeeded());
        const auto registry = create_scene_component_registry();
        for(const auto& asset : database.get_assets()) {
            if(asset.type != AssetType::Scene)
                continue;
            SCOPED_TRACE(asset.path.string());
            const auto scene = SceneSerializer(registry).load((root / asset.path).string());
            ASSERT_TRUE(scene) << scene.error();
            for(const auto& reference : registry.collect_asset_references(*scene.value())) {
                if(const auto* builtin = find_default_asset(reference.handle)) {
                    EXPECT_EQ(reference.type, builtin->type);
                    EXPECT_FALSE(database.find(reference.handle));
                    continue;
                }
                const auto* record = database.find(reference.handle);
                if(reference.required || record)
                    EXPECT_TRUE(identities.contains(reference.handle));
                if(reference.required)
                    ASSERT_NE(record, nullptr);
                if(record)
                    EXPECT_EQ(record->type, reference.type);
            }
        }
        std::map<AssetHandle, MaterialData> model_materials;
        for(const auto& record : database.get_assets()) {
            if(record.type != AssetType::Model)
                continue;
            const auto metadata = MetadataSerializer{}.load(metadata_path(root / record.path));
            ASSERT_TRUE(metadata);
            auto imported = ModelImporter{}.import(root / record.path);
            ASSERT_TRUE(imported) << imported.error();
            auto bound = ModelImporter::bind(std::move(imported).value(), metadata.value());
            ASSERT_TRUE(bound) << bound.error();
            for(auto& material : bound.value().materials)
                EXPECT_TRUE(
                    model_materials.emplace(material.handle, std::move(material.data)).second);
        }
        for(const auto& record : database.get_assets()) {
            if(record.type != AssetType::Material)
                continue;
            MaterialData data;
            if(record.source_asset) {
                const auto found = model_materials.find(record.handle);
                ASSERT_NE(found, model_materials.end());
                data = found->second;
            } else {
                const auto material = MaterialSerializer{}.load(root / record.path);
                ASSERT_TRUE(material) << material.error();
                data = material.value();
            }
            EXPECT_FALSE(data.template_name.empty());
            for(const auto dependency : get_asset_dependencies(data)) {
                const auto* asset = database.find(dependency);
                ASSERT_NE(asset, nullptr);
                auto expected = AssetType::Texture;
                if(dependency == data.shader_program)
                    expected = AssetType::ShaderProgram;
                EXPECT_EQ(asset->type, expected);
            }
        }
    }

    TEST(ProjectAssetsTest, DemoSceneRetainsAudioConfigurationWhenCloned) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        for(const auto* path : {"scenes/default.scene", "scenes/character.scene"}) {
            auto loaded = serializer.load((project.value().paths().assets() / path).string());
            ASSERT_TRUE(loaded) << loaded.error();
            const auto count = loaded.value()->component_count<AudioSourceComponent>();
            ASSERT_GT(count, 0u);
            auto clone = serializer.clone(*loaded.value());
            ASSERT_TRUE(clone) << clone.error();
            EXPECT_EQ(clone.value()->component_count<AudioSourceComponent>(), count);
            loaded.value()->each<const AudioSourceComponent>(
                [&](Entity entity, const auto& source) {
                    const auto copied = clone.value()->find_entity(entity.get_uuid());
                    ASSERT_TRUE(copied.has_component<AudioSourceComponent>());
                    const auto& audio = copied.get_component<AudioSourceComponent>();
                    EXPECT_EQ(audio.clip, source.clip);
                    EXPECT_EQ(audio.play_on_start, source.play_on_start);
                    EXPECT_EQ(audio.loop, source.loop);
                    EXPECT_EQ(audio.volume, source.volume);
                    EXPECT_EQ(audio.category, source.category);
                });
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
