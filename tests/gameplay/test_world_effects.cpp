#include "gameplay/world_effects.h"
#include "gameplay/world_streaming.h"
#include "scene/prefab.h"
#include "gameplay/wind_sway.h"
#include "particles/particle_system.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/material_parameters.h"
#include "scene/scene_serializer.h"
#include "scene/component_registry.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(WorldEffectsTest, CameraRailUsesWorldSpaceAndRestoresManualControl) {
        Scene scene;
        auto path = scene.create_entity("path");
        path.add_component<SplineComponent>(
            SplineComponent{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}});
        path.edit_transform([](auto& t) { t.translation = {10, 0, 0}; });
        auto parent = scene.create_entity("parent");
        parent.edit_transform([](auto& t) { t.translation = {5, 0, 0}; });
        auto camera = scene.create_entity("camera");
        ASSERT_TRUE(scene.set_parent(camera, parent));
        camera.add_component<CameraControllerComponent>();
        camera.add_component<SplineFollowerComponent>(SplineFollowerComponent{
            .path = path.get_uuid(), .duration = 1, .orient = false, .ease = false});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::SplineMotionSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(camera.get_component<CameraControllerComponent>().enabled);
        EXPECT_FLOAT_EQ(Math::Vec3(scene.get_world_matrix(camera)[3]).x, 10);
        ASSERT_TRUE(runtime.advance(.25));
        EXPECT_NEAR(Math::Vec3(scene.get_world_matrix(camera)[3]).x, 10.75f, 1e-5f);
        for(int i = 0; i < 3; ++i)
            ASSERT_TRUE(runtime.advance(.25));
        EXPECT_NEAR(Math::Vec3(scene.get_world_matrix(camera)[3]).x, 13, 1e-5f);
        EXPECT_TRUE(camera.get_component<CameraControllerComponent>().enabled);
        EXPECT_FALSE(camera.get_component<SplineFollowerComponent>().playing);
        ASSERT_TRUE(runtime.stop());
    }
    TEST(WorldEffectsTest, RailRejectsDynamicBodiesAndDependentPaths) {
        Scene scene;
        auto path = scene.create_entity();
        path.add_component<SplineComponent>();
        auto follower = scene.create_entity();
        follower.add_component<SplineFollowerComponent>(
            SplineFollowerComponent{.path = path.get_uuid()});
        follower.add_component<RigidBodyComponent>(
            RigidBodyComponent{.motion = BodyMotion::Dynamic});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::SplineMotionSystem>()));
        EXPECT_FALSE(runtime.start(scene));
        follower.remove_component<RigidBodyComponent>();
        ASSERT_TRUE(scene.set_parent(path, follower));
        EXPECT_FALSE(runtime.start(scene));
    }
    TEST(WorldEffectsTest, WeatherControlsDaylightAndRestoresAuthoredValues) {
        Scene scene;
        const auto original = scene.get_environment();
        auto sun = scene.create_entity();
        sun.add_component<LightComponent>();
        const auto original_sun = sun.get_component<LightComponent>();
        auto rain = scene.create_entity();
        const auto emitter = ParticleEmitterComponent::preset(ParticlePreset::Rain);
        rain.add_component<ParticleEmitterComponent>(emitter);
        auto weather = scene.create_entity();
        weather.add_component<WeatherComponent>(WeatherComponent{.hour = 12,
            .day_length = 1,
            .sun = sun.get_uuid(),
            .precipitation = rain.get_uuid(),
            .precipitation_amount = .5f});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::WeatherSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_GT(sun.get_component<LightComponent>().intensity, 0);
        EXPECT_FLOAT_EQ(rain.get_component<ParticleEmitterComponent>().rate, emitter.rate * .5f);
        EXPECT_EQ(scene.get_environment().wind, Math::Vec3(2, 0, 0));
        // 天气只拥有风、环境光、雾的颜色/密度，以及太阳和降水的指定字段。
        auto unrelated = scene.get_environment();
        unrelated.background_color = {.2f, .3f, .4f};
        unrelated.volumetric_fog.max_distance = 200;
        ASSERT_TRUE(scene.set_environment(unrelated));
        sun.edit_component<LightComponent>([](auto& value) { value.color = {.4f, .5f, .6f}; });
        sun.edit_transform([](auto& value) { value.translation = {1, 2, 3}; });
        rain.edit_component<ParticleEmitterComponent>([](auto& value) { value.start_size = .7f; });
        ASSERT_TRUE(runtime.advance(.25));
        ASSERT_TRUE(runtime.advance(.25));
        EXPECT_NEAR(sun.get_component<LightComponent>().intensity, 0, 1e-5f);
        EXPECT_EQ(scene.get_environment().background_color, unrelated.background_color);
        EXPECT_EQ(sun.get_component<LightComponent>().color, Math::Vec3(.4f, .5f, .6f));
        EXPECT_FLOAT_EQ(rain.get_component<ParticleEmitterComponent>().start_size, .7f);
        ASSERT_TRUE(runtime.stop());
        auto expected_environment = original;
        expected_environment.background_color = unrelated.background_color;
        expected_environment.volumetric_fog.max_distance = unrelated.volumetric_fog.max_distance;
        auto expected_sun = original_sun;
        expected_sun.color = {.4f, .5f, .6f};
        auto expected_emitter = emitter;
        expected_emitter.start_size = .7f;
        EXPECT_EQ(scene.get_environment(), expected_environment);
        EXPECT_EQ(sun.get_component<LightComponent>(), expected_sun);
        EXPECT_EQ(sun.get_component<TransformComponent>().translation, Math::Vec3(1, 2, 3));
        EXPECT_EQ(rain.get_component<ParticleEmitterComponent>(), expected_emitter);
    }
    TEST(WorldEffectsTest, InteractionBendsVegetationWithoutChangingItsTransform) {
        Scene scene;
        auto plant = scene.create_entity();
        plant.add_component<WindSwayComponent>(WindSwayComponent{
            .amplitude = {0, 0, 0}, .flexible = true, .height = 2, .interaction_radius = 1});
        auto actor = scene.create_entity();
        actor.add_component<VegetationInteractorComponent>();
        actor.edit_transform([](auto& t) { t.translation = {-.5f, 0, 0}; });
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::WindSwaySystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.1));
        const auto bend = scene.get_vertex_bend(plant);
        EXPECT_GT(bend.x, 0);
        EXPECT_EQ(plant.get_component<TransformComponent>().rotation, Math::Vec3(0));
        actor.edit_transform([](auto& t) { t.translation = {10, 0, 0}; });
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_LT(scene.get_vertex_bend(plant).x, bend.x);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(scene.get_vertex_bend(plant), Math::Vec4(0));
    }
    TEST(WorldEffectsTest, WaterPublishesAnimationOnlyInRuntimeMaterialOverrides) {
        class Validator final: public MaterialParameterValidator {
            Result<void> validate(const MaterialOverrides&) const override {
                return Result<void>::success();
            }
        } validator;
        Scene scene;
        auto water = scene.create_entity();
        water.add_component<MeshRendererComponent>(
            MeshRendererComponent{.material = AssetHandle(1)});
        water.add_component<WaterSurfaceComponent>();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::WaterSurfaceSystem>(validator)));
        ASSERT_TRUE(runtime.start(scene));
        const auto before = scene.get_material_overrides(water);
        ASSERT_TRUE(before);
        ASSERT_TRUE(runtime.advance(.1));
        const auto after = scene.get_material_overrides(water);
        ASSERT_TRUE(after);
        EXPECT_NE(after->vector_properties.at("water_parameters").z,
            before->vector_properties.at("water_parameters").z);
        EXPECT_EQ(before->vector_properties.at("water_parameters").z, 0);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_material_overrides(water));
    }
    TEST(WorldEffectsTest, WeatherRateAndForceChangesKeepExistingGpuBirths) {
        for(const auto kind : {ParticlePreset::Sparks, ParticlePreset::Smoke, ParticlePreset::Rain,
                ParticlePreset::Snow})
            EXPECT_TRUE(ParticleEmitterComponent::preset(kind).validate());
        Scene scene;
        auto source = scene.create_entity();
        auto settings = ParticleEmitterComponent::preset(ParticlePreset::Rain);
        settings.rate = 0;
        settings.burst = 10;
        source.add_component<ParticleEmitterComponent>(settings);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_EQ(scene.get_particles()->gpu_emitters.size(), 1u);
        auto original = scene.get_particles();
        source.edit_component<ParticleEmitterComponent>([](auto& value) {
            value.rate = 20;
            value.wind = {3, 0, 0};
        });
        ASSERT_TRUE(runtime.advance(.1));
        const auto current = scene.get_particles();
        ASSERT_EQ(current->gpu_emitters.size(), 1u);
        EXPECT_GT(current->gpu_emitters[0].count, original->gpu_emitters[0].count);
        EXPECT_EQ(
            current->gpu_emitters[0].spawns[0].time, original->gpu_emitters[0].spawns[0].time);
        EXPECT_EQ(original->gpu_emitters[0].settings.wind, Math::Vec3(0));
    }
    TEST(WorldStreamingTest,
        WaitsForProductsThenActivatesUnloadsAndCancelsWithoutTouchingOtherContent) {
        const auto components = create_scene_component_registry();
        Scene cell_content;
        auto root = cell_content.create_entity("cell root");
        auto child = cell_content.create_entity("mesh");
        child.add_component<MeshRendererComponent>(
            MeshRendererComponent{AssetHandle(10), AssetHandle(20)});
        ASSERT_TRUE(cell_content.set_parent(child, root));
        auto captured = Prefab::capture(cell_content, components, root);
        ASSERT_TRUE(captured);
        auto prefab = std::make_shared<Prefab>(std::move(captured).value());
        Scene scene;
        auto camera = scene.create_entity("camera");
        camera.add_component<CameraComponent>();
        auto marker = scene.create_entity("cell");
        marker.add_component<StreamCellComponent>(StreamCellComponent{
            .prefab = AssetHandle(30), .load_distance = 10, .unload_distance = 15});
        bool ready = false;
        uint32_t prepared = 0;
        auto streaming = std::make_unique<Gameplay::WorldStreamingSystem>(
            Gameplay::WorldStreamingSystem::Sources{
                .load =
                    [prefab](AssetHandle) {
                        return Result<std::shared_ptr<const Prefab>>::success(prefab);
                    },
                .prepare =
                    [&](Scene& content) {
                        ++prepared;
                        return Result<std::vector<AssetReference>, Error>::success(
                            components.collect_asset_references(content));
                    },
                .ready = [&](auto) { return Result<bool, Error>::success(ready); },
                .resident_bytes = [] { return uint64_t(1); }});
        auto* status = streaming.get();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::move(streaming)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(status->statistics().pending, 1u);
        EXPECT_EQ(scene.get_entities().size(), 2u);
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(prepared, 1u);
        ready = true;
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(status->statistics().active, 1u);
        EXPECT_EQ(scene.get_entities().size(), 4u);
        camera.edit_transform([](auto& t) { t.translation = {100, 0, 0}; });
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(scene.get_entities().size(), 2u);
        EXPECT_TRUE(status->references().empty());
        camera.edit_transform([](auto& t) { t.translation = {0, 0, 0}; });
        ready = false;
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(status->statistics().pending, 1u);
        marker.edit_component<StreamCellComponent>([](auto& cell) { cell.enabled = false; });
        ready = true;
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(scene.get_entities().size(), 2u);
        EXPECT_TRUE(status->references().empty());
        marker.edit_component<StreamCellComponent>([](auto& cell) { cell.enabled = true; });
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(scene.get_entities().size(), 4u);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(scene.get_entities().size(), 2u);
        const auto active =
            components.collect_asset_references(scene, ComponentRegistry::ReferenceScope::Resident);
        EXPECT_TRUE(active.empty());
        const auto exported =
            components.collect_asset_references(scene, ComponentRegistry::ReferenceScope::Runtime);
        ASSERT_EQ(exported.size(), 1u);
        EXPECT_EQ(exported[0].handle, AssetHandle(30));
    }
    TEST(WorldStreamingTest, RetiresOutgoingResourcesBeforeAdmittingTheNextRegion) {
        const auto components = create_scene_component_registry();
        Scene content;
        auto prefab = Prefab::capture(content, components, content.create_entity());
        ASSERT_TRUE(prefab);
        auto source = std::make_shared<Prefab>(std::move(prefab).value());
        Scene scene;
        auto camera = scene.create_entity();
        camera.add_component<CameraComponent>();
        for(int index = 0; index < 2; ++index) {
            auto marker = scene.create_entity();
            marker.edit_transform([&](auto& value) { value.translation.x = index * 30.f; });
            marker.add_component<StreamCellComponent>(StreamCellComponent{
                .prefab = AssetHandle(index + 1), .load_distance = 10, .unload_distance = 20});
        }
        uint64_t bytes = 0;
        int preparations = 0;
        auto system = std::make_unique<Gameplay::WorldStreamingSystem>(
            Gameplay::WorldStreamingSystem::Sources{
                .load =
                    [source](AssetHandle) {
                        return Result<std::shared_ptr<const Prefab>>::success(source);
                    },
                .prepare =
                    [&](Scene&) {
                        ++preparations;
                        bytes += 80;
                        return Result<std::vector<AssetReference>, Error>::success({});
                    },
                .ready = [](auto) { return Result<bool, Error>::success(true); },
                .resident_bytes = [&] { return bytes; }},
            100);
        auto* streaming = system.get();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::move(system)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(streaming->statistics().active, 1u);
        camera.edit_transform([](auto& value) { value.translation.x = 30; });
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(preparations, 1);
        EXPECT_EQ(streaming->statistics().active, 0u);
        EXPECT_TRUE(streaming->references().empty());
        bytes = 0; // 宿主在维护阶段回收退出需求集的资产。
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(preparations, 2);
        EXPECT_EQ(streaming->statistics().active, 1u);
        EXPECT_EQ(streaming->statistics().failed, 0u);
    }
    TEST(WorldStreamingTest, BudgetRejectsNewCellsAndKeepsAnAlreadyActiveCell) {
        const auto components = create_scene_component_registry();
        Scene content;
        auto root = content.create_entity();
        auto prefab = Prefab::capture(content, components, root);
        ASSERT_TRUE(prefab);
        auto source = std::make_shared<Prefab>(std::move(prefab).value());
        Scene scene;
        auto camera = scene.create_entity();
        camera.add_component<CameraComponent>();
        auto a = scene.create_entity();
        a.add_component<StreamCellComponent>(StreamCellComponent{
            .prefab = AssetHandle(1), .load_distance = 10, .unload_distance = 20});
        auto b = scene.create_entity();
        b.edit_transform([](auto& t) { t.translation = {15, 0, 0}; });
        b.add_component<StreamCellComponent>(StreamCellComponent{
            .prefab = AssetHandle(2), .load_distance = 10, .unload_distance = 20});
        uint64_t bytes = 1;
        auto system = std::make_unique<Gameplay::WorldStreamingSystem>(
            Gameplay::WorldStreamingSystem::Sources{
                .load =
                    [source](AssetHandle) {
                        return Result<std::shared_ptr<const Prefab>>::success(source);
                    },
                .prepare =
                    [](Scene&) { return Result<std::vector<AssetReference>, Error>::success({}); },
                .ready = [](auto) { return Result<bool, Error>::success(true); },
                .resident_bytes = [&] { return bytes; }},
            100);
        auto* streaming = system.get();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::move(system)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(streaming->statistics().active, 1u);
        bytes = 101;
        camera.edit_transform([](auto& t) { t.translation = {6, 0, 0}; });
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(streaming->statistics().active, 1u);
        EXPECT_EQ(streaming->statistics().failed, 1u);
        EXPECT_EQ(scene.get_entities().size(), 4u);
    }

}
