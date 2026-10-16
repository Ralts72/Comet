#include "assets/lighting_bake_input.h"
#include "assets/reflection_bake.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/data/irradiance_data.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <algorithm>

namespace CometEditor::Tests {
    using namespace Comet;
    TEST(ReflectionBakeTest, StaticDecalFingerprintIncludesTextureAndIgnoresVolumetricFog) {
        Comet::Tests::TemporaryDirectory directory;
        const ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto texture = paths.assets() / "paint.png";
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png",
            texture);
        const AssetHandle handle(80303);
        ASSERT_TRUE(MetadataSerializer{}.save({.handle = handle,
                                                  .type = AssetType::Texture,
                                                  .import_settings = TextureImportSettings{}},
            metadata_path(texture)));
        AssetDatabase assets(paths);
        ASSERT_TRUE(assets.scan().succeeded());
        Scene scene;
        auto decal = scene.create_entity("static decal");
        decal.add_component<DecalComponent>(DecalComponent{.texture = handle});
        auto moving = scene.create_entity("moving decal");
        moving.add_component<RigidBodyComponent>();
        moving.add_component<DecalComponent>();
        const auto describe = [&](LightingBakeInput::Purpose purpose) {
            return LightingBakeInput::describe(scene, assets, purpose, 123);
        };
        const auto original = describe(LightingBakeInput::Purpose::Reflection);
        ASSERT_TRUE(original) << original.error();
        ASSERT_EQ(original.value().scene.decals.size(), 1u);
        EXPECT_NE(std::ranges::find(original.value().dependencies, handle),
            original.value().dependencies.end());
        auto environment = scene.get_environment();
        environment.volumetric_fog.enabled = true;
        ASSERT_TRUE(scene.set_environment(environment));
        moving.edit_component<DecalComponent>([](auto& value) { value.color.r = .2f; });
        auto unchanged = describe(LightingBakeInput::Purpose::Reflection);
        ASSERT_TRUE(unchanged);
        EXPECT_EQ(unchanged.value().fingerprint, original.value().fingerprint);
        EXPECT_FALSE(unchanged.value().scene.environment.volumetric_fog.active());
        const auto gi = describe(LightingBakeInput::Purpose::Irradiance);
        ASSERT_TRUE(gi);
        EXPECT_TRUE(gi.value().scene.decals.empty());
        decal.edit_component<DecalComponent>([](auto& value) { value.color.r = .3f; });
        const auto changed = describe(LightingBakeInput::Purpose::Reflection);
        ASSERT_TRUE(changed);
        EXPECT_NE(changed.value().fingerprint, original.value().fingerprint);
        const auto same_gi = describe(LightingBakeInput::Purpose::Irradiance);
        ASSERT_TRUE(same_gi);
        EXPECT_EQ(same_gi.value().fingerprint, gi.value().fingerprint);
    }

    TEST(ReflectionBakeTest, FingerprintTracksStaticInputsAndSurvivesSceneReopen) {
        Comet::Tests::TemporaryDirectory directory;
        const ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto mesh_path = paths.assets() / "cube.gltf";
        std::filesystem::copy_file(
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
            mesh_path);
        const AssetHandle mesh(80301);
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = mesh, .type = AssetType::Mesh}, metadata_path(mesh_path)));
        MaterialData material{
            .template_name = "unlit_color", .vector_properties = {{"color", {4, 2, 1, 1}}}};
        ASSERT_TRUE(MaterialSerializer{}.save(material, paths.assets() / "wall.mat"));
        AssetDatabase assets(paths);
        ASSERT_TRUE(assets.scan().succeeded());
        const auto wall = assets.find("wall.mat")->handle;
        Scene scene;
        auto surface = scene.create_entity("static");
        surface.add_component<MeshRendererComponent>(MeshRendererComponent{mesh, wall});
        auto moving = scene.create_entity("moving");
        moving.add_component<RigidBodyComponent>();
        auto child = scene.create_entity("moving child");
        child.add_component<MeshRendererComponent>(MeshRendererComponent{mesh, wall});
        ASSERT_TRUE(scene.set_parent(child, moving));
        auto light = scene.create_entity("sun");
        light.add_component<LightComponent>();
        auto probe = scene.create_entity("probe");
        probe.add_component<ReflectionProbeComponent>();
        const auto original = ReflectionBake::describe_input(scene, probe.get_uuid(), assets, 123);
        ASSERT_TRUE(original) << original.error();
        ASSERT_EQ(original.value().scene.render_items.size(), 1u);
        EXPECT_EQ(original.value().scene.render_items[0].entity_id, surface.get_id());
        EXPECT_EQ(original.value().scene.lights.size(), 1u);
        EXPECT_TRUE(original.value().scene.reflection_probes.empty());
        const auto fingerprint = [&] {
            auto input = ReflectionBake::describe_input(scene, probe.get_uuid(), assets, 123);
            EXPECT_TRUE(input) << input.error();
            return input ? input.value().fingerprint : std::string{};
        };
        auto environment = scene.get_environment();
        environment.fog_enabled = true;
        ASSERT_TRUE(scene.set_environment(environment));
        auto post = scene.get_post_process();
        post.exposure = 2;
        post.bloom_enabled = true;
        ASSERT_TRUE(scene.set_post_process(post));
        probe.edit_component<ReflectionProbeComponent>([](auto& value) {
            value.environment = AssetHandle(9812);
            value.intensity = 3;
            value.rotation = 40;
            value.half_extents = {2, 3, 4};
            value.bake_fingerprint = "previous capture";
        });
        moving.edit_transform([](auto& value) { value.translation.x = 4; });
        EXPECT_EQ(fingerprint(), original.value().fingerprint);
        const auto components = create_scene_component_registry();
        SceneSerializer serializer(components);
        auto text = serializer.serialize(scene);
        ASSERT_TRUE(text) << text.error();
        auto reopened = serializer.deserialize(text.value());
        ASSERT_TRUE(reopened) << reopened.error();
        AssetDatabase reopened_assets(paths);
        ASSERT_TRUE(reopened_assets.scan().succeeded());
        auto reopened_input = ReflectionBake::describe_input(
            *reopened.value(), probe.get_uuid(), reopened_assets, 123);
        ASSERT_TRUE(reopened_input) << reopened_input.error();
        EXPECT_EQ(reopened_input.value().fingerprint, original.value().fingerprint);

        surface.edit_transform([](auto& value) { value.translation.x = 1; });
        EXPECT_NE(fingerprint(), original.value().fingerprint);
        surface.edit_transform([](auto& value) { value.translation.x = 0; });
        EXPECT_EQ(fingerprint(), original.value().fingerprint);
        light.edit_component<LightComponent>([](auto& value) { value.intensity *= 2; });
        EXPECT_NE(fingerprint(), original.value().fingerprint);
        light.edit_component<LightComponent>([](auto& value) { value.intensity /= 2; });
        EXPECT_EQ(fingerprint(), original.value().fingerprint);
        auto changed_shader = ReflectionBake::describe_input(scene, probe.get_uuid(), assets, 124);
        ASSERT_TRUE(changed_shader);
        EXPECT_NE(changed_shader.value().fingerprint, original.value().fingerprint);
        IrradianceData gi;
        gi.probes.resize(8);
        const auto gi_path = paths.assets() / "room.gi";
        ASSERT_TRUE(gi.save(gi_path));
        const AssetHandle gi_handle(80302);
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = gi_handle, .type = AssetType::Irradiance}, metadata_path(gi_path)));
        ASSERT_TRUE(assets.scan().succeeded());
        auto volume = scene.create_entity("GI");
        volume.add_component<IrradianceVolumeComponent>(
            IrradianceVolumeComponent{.irradiance = gi_handle});
        const auto with_gi = fingerprint();
        EXPECT_NE(with_gi, original.value().fingerprint);
        volume.edit_component<IrradianceVolumeComponent>(
            [](auto& value) { value.intensity = .5f; });
        EXPECT_NE(fingerprint(), with_gi);
        volume.edit_component<IrradianceVolumeComponent>([](auto& value) { value.intensity = 1; });
        EXPECT_EQ(fingerprint(), with_gi);
        gi.probes[0].coefficients[0].x = 1;
        ASSERT_TRUE(gi.save(gi_path));
        EXPECT_NE(fingerprint(), with_gi);
        volume.edit_component<IrradianceVolumeComponent>(
            [](auto& value) { value.enabled = false; });
        EXPECT_EQ(fingerprint(), original.value().fingerprint);
        material.vector_properties["color"] = {1, 2, 4, 1};
        ASSERT_TRUE(MaterialSerializer{}.save(material, paths.assets() / "wall.mat"));
        EXPECT_NE(fingerprint(), original.value().fingerprint);
        probe.edit_component<ReflectionProbeComponent>(
            [](auto& value) { value.capture_resolution = 17; });
        EXPECT_FALSE(ReflectionBake::describe_input(scene, probe.get_uuid(), assets, 123));
    }
}
