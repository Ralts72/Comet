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
#include "scene/systems/physics_system.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    TEST(ParticleSystemTest, RenderOnlyFramesReuseSnapshotsAndObserveSameFrameSourceChanges) {
        Scene scene;
        auto parent = scene.create_entity();
        auto source = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(source, parent));
        ParticleEmitterComponent settings;
        settings.burst = 2;
        settings.rate = 0;
        source.add_component<ParticleEmitterComponent>(settings);
        auto particles = std::make_unique<ParticleSystem>();
        const auto* observed = particles.get();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = .1}));
        ASSERT_TRUE(runtime.add_system(std::move(particles)));
        ASSERT_TRUE(runtime.start(scene));
        const auto first = scene.get_particles();
        const auto before = observed->get_statistics();
        ASSERT_TRUE(first);
        for(int frame = 0; frame < 10; ++frame)
            ASSERT_TRUE(runtime.advance(.001));
        EXPECT_EQ(scene.get_particles(), first);
        EXPECT_EQ(observed->get_statistics().source_scans, before.source_scans);
        EXPECT_EQ(observed->get_statistics().snapshot_builds, before.snapshot_builds);

        source.edit_component<ParticleEmitterComponent>(
            [](auto& emitter) { emitter.texture = AssetHandle{42}; });
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(scene.get_particles());
        EXPECT_EQ(scene.get_particles()->batches.front().texture, AssetHandle{42});
        EXPECT_EQ(first->batches.front().texture, AssetHandle{});
        parent.edit_transform([](auto& transform) { transform.translation.x = 10; });
        ASSERT_TRUE(scene.request_particle_burst(source, 1));
        source.remove_component<ParticleEmitterComponent>();
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(scene.get_particles());
        ASSERT_EQ(scene.get_particles()->instances.size(), 3u);
        EXPECT_FLOAT_EQ(scene.get_particles()->instances.back().position_size.x, 10);
        ASSERT_TRUE(scene.request_particle_clear(source));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(scene.get_particles());
        EXPECT_EQ(first->instances.size(), 2u);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(scene.get_particles());
    }

    TEST(ParticleSystemTest, CapturedBurstPoseDoesNotReplaceTheLiveEmitterPose) {
        Scene scene;
        auto parent = scene.create_entity();
        auto source = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(source, parent));
        ParticleEmitterComponent settings;
        settings.rate = 10;
        settings.speed = 0;
        settings.acceleration = {};
        source.add_component<ParticleEmitterComponent>(settings);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = .1}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.request_particle_burst(source, 1));
        parent.edit_transform([](auto& transform) { transform.translation.x = 10; });
        ASSERT_TRUE(runtime.advance(0));
        const auto burst = scene.get_particles();
        ASSERT_TRUE(burst);
        EXPECT_FLOAT_EQ(burst->instances.front().position_size.x, 0);
        ASSERT_TRUE(runtime.advance(.1));
        ASSERT_TRUE(scene.get_particles());
        ASSERT_EQ(scene.get_particles()->instances.size(), 2u);
        EXPECT_FLOAT_EQ(scene.get_particles()->instances[0].position_size.x, 0);
        EXPECT_FLOAT_EQ(scene.get_particles()->instances[1].position_size.x, 10);
        EXPECT_FLOAT_EQ(burst->instances.front().position_size.x, 0);
    }

    TEST(ParticleSystemTest, TrailsReusePublishedSegmentsBetweenFixedStepsAndApplyEdits) {
        Scene scene;
        auto source = scene.create_entity();
        source.add_component<TrailComponent>();
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = .1}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        source.edit_transform([](auto& transform) { transform.translation.x = 1; });
        ASSERT_TRUE(runtime.advance(.1));
        const auto first = scene.get_particles();
        ASSERT_TRUE(first);
        ASSERT_TRUE(runtime.advance(.01));
        EXPECT_EQ(scene.get_particles(), first);
        source.edit_component<TrailComponent>([](auto& trail) { trail.width = .5f; });
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(scene.get_particles());
        EXPECT_FLOAT_EQ(scene.get_particles()->instances.front().end_position_size.w, .5f);
        EXPECT_NE(scene.get_particles(), first);
        EXPECT_FLOAT_EQ(first->instances.front().end_position_size.w, .15f);
    }

    TEST(ParticleSystemTest, SweptCollisionsBounceOrKillAndRespectLayers) {
        for(const auto mode :
            {ParticleCollision::None, ParticleCollision::Bounce, ParticleCollision::Kill}) {
            for(const auto layers : {1u, 2u}) {
                SCOPED_TRACE(static_cast<int>(mode));
                SCOPED_TRACE(layers);
                Scene scene;
                auto ground = scene.create_entity("ground");
                ground.edit_transform([](auto& transform) { transform.translation.y = -.1f; });
                ground.add_component<RigidBodyComponent>(
                    RigidBodyComponent{.motion = BodyMotion::Static});
                ground.add_component<ColliderComponent>(
                    ColliderComponent{.half_extents = {5, .1f, 5}});
                auto entity = scene.create_entity("spark");
                entity.edit_transform([](auto& transform) {
                    transform.translation.y = 1;
                    transform.rotation.x = 180;
                });
                ParticleEmitterComponent emitter;
                emitter.rate = 0;
                emitter.burst = 1;
                emitter.spread = 0;
                emitter.speed = 20;
                emitter.acceleration = {};
                emitter.collision = mode;
                emitter.collision_radius = .05f;
                emitter.collision_layers = layers;
                entity.add_component<ParticleEmitterComponent>(emitter);
                PhysicsService physics;
                SceneRuntime runtime;
                ASSERT_TRUE(runtime.set_services({.physics = &physics}));
                ASSERT_TRUE(runtime.set_settings({.fixed_delta = .1, .max_frame_delta = 1}));
                ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
                ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
                ASSERT_TRUE(runtime.start(scene));
                const auto initial = scene.get_particles();
                ASSERT_TRUE(runtime.advance(.1));
                if(layers == 1 && mode == ParticleCollision::Kill)
                    EXPECT_FALSE(scene.get_particles());
                else {
                    ASSERT_TRUE(scene.get_particles());
                    const float y = scene.get_particles()->instances.front().position_size.y;
                    if(layers == 1 && mode == ParticleCollision::Bounce) {
                        EXPECT_GT(y, .5f);
                        EXPECT_LT(y, .65f);
                    } else
                        EXPECT_NEAR(y, -1, 1e-5);
                }
                EXPECT_FLOAT_EQ(initial->instances.front().position_size.y, 1);
            }
        }
    }

    TEST(ParticleSystemTest, TrailsBreakOnTeleportAndExpireWithoutChangingPublishedSnapshots) {
        Scene scene;
        auto entity = scene.create_entity("trail");
        TrailComponent settings;
        settings.lifetime = .2f;
        settings.capacity = 8;
        entity.add_component<TrailComponent>(settings);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings(
            {.fixed_delta = .05, .max_frame_delta = 1, .max_fixed_steps = 16}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(scene.get_particles());
        entity.edit_transform([](auto& transform) { transform.translation.x = .5f; });
        ASSERT_TRUE(runtime.advance(.05));
        const auto first = scene.get_particles();
        ASSERT_TRUE(first);
        ASSERT_EQ(first->instances.size(), 1u);
        EXPECT_FLOAT_EQ(first->instances[0].end_position_size.x, .5f);
        EXPECT_GE(first->instances[0].end_position_size.w, 0);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(.5));
        EXPECT_EQ(scene.get_particles(), first);
        entity.edit_transform([](auto& transform) { transform.translation.x = 100; });
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(scene.get_particles());
        EXPECT_EQ(scene.get_particles()->instances.size(), 1u);
        entity.edit_transform([](auto& transform) { transform.translation.x = 100.5f; });
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_EQ(scene.get_particles()->instances.size(), 2u);
        for(const auto& segment : scene.get_particles()->instances)
            EXPECT_LE(Math::length(Math::Vec3(segment.end_position_size)
                                   - Math::Vec3(segment.position_size)),
                settings.break_distance);
        scene.destroy_entity(entity);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(.3));
        EXPECT_FALSE(scene.get_particles());
        EXPECT_FLOAT_EQ(first->instances[0].end_position_size.x, .5f);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_particles());
    }

    TEST(ParticleSystemTest, TrailConfigurationRoundTripsAndCollectsItsTexture) {
        TemporaryDirectory directory;
        Scene scene;
        auto entity = scene.create_entity("trail");
        TrailComponent trail;
        trail.texture = AssetHandle{1234};
        trail.blend = ParticleBlend::Alpha;
        entity.add_component<TrailComponent>(trail);
        auto components = create_scene_component_registry();
        const auto references = components.collect_asset_references(scene);
        ASSERT_EQ(references.size(), 1u);
        EXPECT_EQ(references[0].handle, trail.texture);
        SceneSerializer serializer(components);
        const auto path = directory.path() / "trail.scene";
        ASSERT_TRUE(serializer.save(scene, path.string()));
        auto loaded = serializer.load(path.string());
        ASSERT_TRUE(loaded);
        EXPECT_EQ(
            loaded.value()->find_entity(entity.get_uuid()).get_component<TrailComponent>(), trail);
        auto invalid = trail;
        invalid.capacity = 1;
        EXPECT_FALSE(invalid.validate());
        invalid = trail;
        invalid.minimum_distance = invalid.break_distance;
        EXPECT_FALSE(invalid.validate());
    }

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
            emitter.atlas_columns = emitter.atlas_rows = 2;
            emitter.frame_count = 4;
            emitter.frame_rate = 7;
            emitter.loop_animation = true;
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

    TEST(ParticleSystemTest, FlipbookUsesParticleAgeAndDoesNotAdvanceWhilePaused) {
        ParticleEmitterComponent settings;
        settings.atlas_columns = settings.atlas_rows = 2;
        settings.frame_count = 3;
        settings.lifetime = 1.5f;
        ASSERT_TRUE(settings.validate());
        EXPECT_EQ(settings.texture_region(0), (Math::Vec4{0, 0, .5f, .5f}));
        EXPECT_EQ(settings.texture_region(.5), (Math::Vec4{.5f, 0, .5f, .5f}));
        EXPECT_EQ(settings.texture_region(1.49), (Math::Vec4{0, .5f, .5f, .5f}));
        settings.frame_rate = 4;
        EXPECT_EQ(settings.texture_region(1), (Math::Vec4{0, .5f, .5f, .5f}));
        settings.loop_animation = true;
        EXPECT_EQ(settings.texture_region(1), (Math::Vec4{.5f, 0, .5f, .5f}));
        settings.rate = 0;
        settings.burst = 1;
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<ParticleEmitterComponent>(settings);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = .05, .max_frame_delta = 1}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto initial = scene.get_particles();
        ASSERT_TRUE(runtime.advance(.3));
        EXPECT_EQ(scene.get_particles()->instances[0].uv_rect, settings.texture_region(.3));
        EXPECT_EQ(initial->instances[0].uv_rect, settings.texture_region(0));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        const auto paused = scene.get_particles();
        ASSERT_TRUE(runtime.advance(.5));
        EXPECT_EQ(scene.get_particles(), paused);
    }

    TEST(ParticleSystemTest, SoftDistancesSplitBatchesButFlipbookLayoutsCanShareATexture) {
        Scene scene;
        ParticleEmitterComponent settings;
        settings.rate = 0;
        settings.burst = 2;
        settings.texture = AssetHandle(11);
        for(int i = 0; i < 3; ++i) {
            settings.soft_distance = i == 0 ? 0 : .5f;
            settings.atlas_columns = settings.frame_count = i + 1;
            scene.create_entity().add_component<ParticleEmitterComponent>(settings);
        }
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ParticleSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto snapshot = scene.get_particles();
        ASSERT_TRUE(snapshot);
        ASSERT_EQ(snapshot->batches.size(), 2u);
        EXPECT_EQ(snapshot->batches[0].count, 2u);
        EXPECT_EQ(snapshot->batches[1].count, 4u);
        EXPECT_TRUE(snapshot->uses_soft_depth());
        auto empty = *snapshot;
        for(auto& batch : empty.batches)
            batch.count = 0;
        EXPECT_FALSE(empty.uses_soft_depth());
    }

    TEST(ParticleSystemTest, RejectsInvalidAtlasFramesRatesAndSoftDistances) {
        ParticleEmitterComponent settings;
        for(const auto columns : {0u, 65u, std::numeric_limits<uint32_t>::max()}) {
            settings.atlas_columns = columns;
            EXPECT_FALSE(settings.validate());
        }
        settings = {};
        settings.frame_count = 2;
        EXPECT_FALSE(settings.validate());
        settings = {};
        settings.frame_rate = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(settings.validate());
        settings = {};
        settings.soft_distance = -1;
        EXPECT_FALSE(settings.validate());
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
        settings.soft_distance = .4f;
        settings.atlas_columns = settings.atlas_rows = 4;
        settings.frame_count = 13;
        settings.frame_rate = 10;
        settings.loop_animation = true;
        settings.collision = ParticleCollision::Bounce;
        settings.collision_radius = .04f;
        settings.restitution = .7f;
        settings.friction = .3f;
        settings.collision_layers = 17;
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
