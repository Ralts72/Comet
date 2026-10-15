#include "particles/particle_system.h"
#include "render/scene/scene_extractor.h"
#include "scene/scene_runtime.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "support/temporary_directory.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    TEST(ParticleSystemTest, AdditiveParticlesWithTheSameTextureShareOneBatch) {
        Scene scene;
        for(const auto texture : {AssetHandle{11}, AssetHandle{}, AssetHandle{11}}) {
            ParticleEmitterComponent settings;
            settings.texture = texture;
            settings.rate = 0;
            settings.burst = 4;
            scene.create_entity().add_component<ParticleEmitterComponent>(settings);
        }
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto frame = scene.get_particles();
        ASSERT_TRUE(frame);
        EXPECT_EQ(frame->instances.size(), 12u);
        ASSERT_EQ(frame->batches.size(), 2u);
        EXPECT_EQ(frame->batches[0].texture, AssetHandle{});
        EXPECT_EQ(frame->batches[0].first, 0u);
        EXPECT_EQ(frame->batches[0].count, 4u);
        EXPECT_EQ(frame->batches[1].texture, AssetHandle{11});
        EXPECT_EQ(frame->batches[1].first, 4u);
        EXPECT_EQ(frame->batches[1].count, 8u);
    }

    TEST(ParticleSystemTest, BlendModesRemainSeparateAndLiveChangesKeepOldSnapshots) {
        Scene scene;
        ParticleEmitterComponent settings;
        settings.texture = AssetHandle{11};
        settings.rate = 0;
        settings.burst = 4;
        auto spark = scene.create_entity();
        spark.add_component<ParticleEmitterComponent>(settings);
        settings.blend = ParticleBlend::Alpha;
        scene.create_entity().add_component<ParticleEmitterComponent>(settings);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto first = scene.get_particles();
        ASSERT_EQ(first->batches.size(), 2u);
        EXPECT_EQ(first->batches[0].blend, ParticleBlend::Additive);
        EXPECT_EQ(first->batches[1].blend, ParticleBlend::Alpha);
        ASSERT_TRUE(spark.try_set_component(settings));
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_EQ(scene.get_particles()->batches.size(), 1u);
        EXPECT_EQ(scene.get_particles()->batches[0].blend, ParticleBlend::Alpha);
        EXPECT_EQ(scene.get_particles()->batches[0].count, 8u);
        EXPECT_EQ(first->batches[0].blend, ParticleBlend::Additive);
        EXPECT_EQ(first->instances.size(), 8u);
    }

    TEST(ParticleSystemTest, LuaBurstIsDeliveredInTheSameFrameAfterTheEmitterIsDestroyed) {
        AssetRegistry assets;
        auto source = Script::create(R"(
            return { update = function()
                comet.emit_particles(5, comet.self_entity())
                comet.translate(100, 0, 0)
                comet.destroy_entity(comet.self_entity())
            end }
        )");
        ASSERT_TRUE(source);
        ASSERT_TRUE(assets.register_asset(AssetHandle{9001}, source.value()));
        Scene scene;
        auto entity = scene.create_entity();
        ParticleEmitterComponent emitter;
        emitter.rate = 0;
        entity.add_component<ParticleEmitterComponent>(emitter);
        entity.add_component<ScriptComponent>().asset = AssetHandle{9001};
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(entity);
        ASSERT_TRUE(scene.get_particles());
        ASSERT_EQ(scene.get_particles()->instances.size(), 5u);
        EXPECT_EQ(
            Math::Vec3(scene.get_particles()->instances.front().position_size), Math::Vec3(0));
    }

    TEST(ParticleSystemTest, FixedStepsGiveTheSameParticlesAcrossFramePartitions) {
        const auto simulate = [](double frame_delta, unsigned frames) {
            Scene scene;
            auto entity = scene.create_entity();
            ParticleEmitterComponent emitter;
            emitter.seed = 42;
            emitter.rate = 80;
            emitter.burst = 5;
            entity.add_component<ParticleEmitterComponent>(emitter);
            SceneRuntime runtime;
            EXPECT_TRUE(runtime.set_settings({.fixed_delta = 0.05, .max_fixed_steps = 8}));
            EXPECT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
            EXPECT_TRUE(runtime.start(scene));
            for(unsigned frame = 0; frame < frames; ++frame)
                EXPECT_TRUE(runtime.advance(frame_delta));
            return scene.get_particles()->instances;
        };
        const auto a = simulate(0.05, 16), b = simulate(0.1, 8);
        ASSERT_FALSE(a.empty());
        EXPECT_EQ(a, b);
    }

    TEST(ParticleSystemTest, PauseStepClearStopAndRestartKeepSnapshotsIsolated) {
        Scene scene;
        auto entity = scene.create_entity();
        ParticleEmitterComponent settings;
        settings.burst = 3;
        settings.rate = 0;
        settings.lifetime = 0.2f;
        entity.add_component<ParticleEmitterComponent>(settings);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.max_fixed_steps = 32}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene, SceneRuntime::State::Paused));
        const auto original = scene.get_particles();
        const auto values = original->instances;
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(scene.get_particles(), original);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_NE(scene.get_particles(), original);
        EXPECT_EQ(original->instances, values);
        EXPECT_NE(scene.get_particles()->instances, values);
        ASSERT_TRUE(scene.request_particle_clear(entity));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(scene.get_particles());
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_EQ(scene.get_particles()->instances.size(), 3u);
        ASSERT_TRUE(runtime.advance(0.25));
        EXPECT_FALSE(scene.get_particles());
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_particles());
        EXPECT_FALSE(scene.request_particle_burst(entity, 1));
    }

    TEST(ParticleSystemTest, BurstsCaptureWorldPoseAndFinishAfterSourceDestruction) {
        Scene scene;
        auto parent = scene.create_entity();
        parent.edit_transform([](auto& transform) { transform.translation = {10, 0, 0}; });
        auto entity = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(entity, parent));
        entity.edit_transform([](auto& transform) { transform.translation = {2, 3, 0}; });
        ParticleEmitterComponent emitter;
        emitter.rate = 0;
        emitter.speed = 0;
        emitter.acceleration = {};
        emitter.lifetime = 0.1f;
        emitter.texture = AssetHandle{42};
        entity.add_component<ParticleEmitterComponent>(emitter);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.request_particle_burst(entity, 7));
        entity.edit_transform([](auto& transform) { transform.translation = {100, 0, 0}; });
        scene.destroy_entity(entity);
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(scene.get_particles());
        ASSERT_EQ(scene.get_particles()->instances.size(), 7u);
        EXPECT_EQ(Math::Vec3(scene.get_particles()->instances.front().position_size),
            (Math::Vec3{12, 3, 0}));
        auto render_scene = SceneExtractor::extract(scene);
        EXPECT_EQ(render_scene.particles, scene.get_particles());
        auto components = create_scene_component_registry();
        const auto references =
            components.collect_asset_references(scene, ComponentRegistry::ReferenceScope::Runtime);
        ASSERT_EQ(references.size(), 1u);
        EXPECT_EQ(references.front().handle, AssetHandle{42});
        EXPECT_TRUE(components.collect_asset_references(scene).empty());
        ASSERT_TRUE(runtime.advance(0.2));
        EXPECT_FALSE(scene.get_particles());
        ASSERT_TRUE(render_scene.particles);
        EXPECT_EQ(render_scene.particles->instances.size(), 7u);
    }

    TEST(ParticleSystemTest, EmitterAndSceneBudgetsDropExcessAndInvalidDataFails) {
        Scene scene;
        for(unsigned index = 0; index < 20; ++index) {
            auto entity = scene.create_entity();
            ParticleEmitterComponent emitter;
            emitter.capacity = emitter.burst = 4096;
            emitter.rate = 0;
            entity.add_component<ParticleEmitterComponent>(emitter);
        }
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_EQ(scene.get_particles()->instances.size(), ParticleSnapshot::MAX_PARTICLES);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_particles());
        ParticleEmitterComponent invalid;
        invalid.lifetime = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(invalid.validate());
        invalid = {};
        invalid.capacity = 0;
        EXPECT_FALSE(invalid.validate());
        invalid = {};
        invalid.start_color.w = 2;
        EXPECT_FALSE(invalid.validate());
        invalid = {};
        invalid.blend = static_cast<ParticleBlend>(99);
        EXPECT_FALSE(invalid.validate());
    }

    TEST(ParticleSystemTest, SceneRoundTripKeepsConfigurationAndDropsLiveParticles) {
        TemporaryDirectory directory;
        Scene scene;
        auto entity = scene.create_entity("effect");
        ParticleEmitterComponent settings;
        settings.seed = 123;
        settings.burst = 12;
        settings.spread = 90;
        settings.texture = AssetHandle{987};
        settings.blend = ParticleBlend::Alpha;
        entity.add_component<ParticleEmitterComponent>(settings);
        auto components = create_scene_component_registry();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.get_particles());
        const auto file = directory.path() / "effect.scene";
        SceneSerializer serializer(components);
        ASSERT_TRUE(serializer.save(scene, file.string()));
        auto loaded = serializer.load(file.string());
        ASSERT_TRUE(loaded);
        auto copy = loaded.value()->find_entity(entity.get_uuid());
        ASSERT_TRUE(copy);
        EXPECT_EQ(copy.get_component<ParticleEmitterComponent>(), settings);
        EXPECT_FALSE(loaded.value()->get_particles());
    }
}
