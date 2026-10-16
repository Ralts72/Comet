#include "inspector/particle_preview.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(ParticlePreviewTest, TrailPreviewSamplesEditsWithoutRunningAProjectScript) {
        using namespace Comet;
        Scene scene;
        auto entity = scene.create_entity("trail");
        TrailComponent trail;
        ParticleEmitterComponent emitter;
        emitter.enabled = false;
        ParticlePreview preview;
        const ParticlePreview::Target target{scene.get_lifetime(), entity.get_uuid()};
        ASSERT_TRUE(preview.bind(target, emitter, scene.get_world_matrix(entity), &scene, &trail));
        ASSERT_TRUE(preview.play());
        entity.edit_transform([](auto& transform) { transform.translation.x = 1; });
        ASSERT_TRUE(preview.bind(target, emitter, scene.get_world_matrix(entity), &scene, &trail));
        ASSERT_TRUE(preview.advance(.05));
        ASSERT_TRUE(preview.snapshot());
        EXPECT_GT(preview.snapshot()->instances.front().end_position_size.w, 0);
        EXPECT_FLOAT_EQ(preview.snapshot()->instances.front().end_position_size.x, 1);
        EXPECT_FALSE(scene.get_particles());
        preview.stop();
        EXPECT_FALSE(preview.snapshot());
    }

    TEST(ParticlePreviewTest, CollidesWithFrozenSceneGeometryWithoutMovingTheEditWorld) {
        using namespace Comet;
        Scene scene;
        auto ground = scene.create_entity("ground");
        ground.edit_transform([](auto& transform) { transform.translation.y = -.1f; });
        ground.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Dynamic});
        ground.add_component<ColliderComponent>(ColliderComponent{.half_extents = {5, .1f, 5}});
        auto entity = scene.create_entity("spark");
        entity.edit_transform([](auto& transform) {
            transform.translation.y = 1;
            transform.rotation.x = 180;
        });
        ParticleEmitterComponent settings;
        settings.rate = 0;
        settings.burst = 1;
        settings.speed = 20;
        settings.spread = 0;
        settings.acceleration = {};
        settings.collision = ParticleCollision::Bounce;
        settings.collision_radius = .05f;
        entity.add_component<ParticleEmitterComponent>(settings);
        ParticlePreview preview;
        ASSERT_TRUE(preview.bind({scene.get_lifetime(), entity.get_uuid()}, settings,
            scene.get_world_matrix(entity), &scene));
        ASSERT_TRUE(preview.play());
        ASSERT_TRUE(preview.advance(.1));
        ASSERT_TRUE(preview.snapshot());
        EXPECT_GT(preview.snapshot()->instances[0].position_size.y, .05f);
        EXPECT_FLOAT_EQ(ground.get_component<TransformComponent>().translation.y, -.1f);
        EXPECT_FALSE(scene.get_particles());
    }

    TEST(ParticlePreviewTest, UsesOnlyAnIsolatedEmitterAndDoesNotSaveItsLiveParticles) {
        Comet::Scene scene;
        auto entity = scene.create_entity("effect");
        Comet::ParticleEmitterComponent settings;
        settings.rate = 0;
        settings.burst = 8;
        settings.spread = 0;
        settings.acceleration = {};
        entity.add_component<Comet::ParticleEmitterComponent>(settings);
        entity.add_component<Comet::ScriptComponent>().asset = Comet::AssetHandle{99999};
        entity.edit_transform([](auto& transform) {
            transform.translation = {5, 3, 0};
            transform.rotation = {0, 0, 90};
        });
        ParticlePreview preview;
        ASSERT_TRUE(preview.bind(
            {scene.get_lifetime(), entity.get_uuid()}, settings, scene.get_world_matrix(entity)));
        ASSERT_TRUE(preview.play());
        ASSERT_TRUE(preview.advance(0.05));
        const auto snapshot = preview.snapshot();
        ASSERT_TRUE(snapshot);
        ASSERT_EQ(snapshot->instances.size(), 8u);
        EXPECT_LT(snapshot->instances.front().position_size.x, 5);
        EXPECT_NEAR(snapshot->instances.front().position_size.y, 3, 1e-5);
        EXPECT_FALSE(scene.get_particles());
        EXPECT_FALSE(scene.request_particle_burst(entity, 1));
        scene.set_particles(snapshot);
        auto components = Comet::create_scene_component_registry();
        Comet::SceneSerializer serializer(components);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned);
        EXPECT_FALSE(cloned.value()->get_particles());
        EXPECT_EQ(cloned.value()
                      ->find_entity(entity.get_uuid())
                      .get_component<Comet::ParticleEmitterComponent>(),
            settings);
    }

    TEST(ParticlePreviewTest, PauseStepAndConfigurationChangesPreserveTimeAndOldSnapshots) {
        Comet::Scene scene;
        auto entity = scene.create_entity();
        const ParticlePreview::Target target{scene.get_lifetime(), entity.get_uuid()};
        Comet::ParticleEmitterComponent settings;
        settings.rate = 0;
        settings.burst = 4;
        settings.texture = Comet::AssetHandle{11};
        ParticlePreview preview;
        ASSERT_TRUE(preview.bind(target, settings, Comet::Math::Mat4(1)));
        ASSERT_TRUE(preview.play());
        preview.pause();
        const auto first = preview.snapshot();
        ASSERT_TRUE(preview.advance(1));
        EXPECT_EQ(preview.snapshot(), first);
        settings.texture = Comet::AssetHandle{12};
        settings.blend = Comet::ParticleBlend::Alpha;
        settings.start_color = {0, 0, 1, 1};
        settings.soft_distance = 0.4f;
        settings.atlas_columns = 2;
        settings.atlas_rows = 2;
        settings.frame_count = 4;
        settings.frame_rate = 90;
        ASSERT_TRUE(preview.bind(target, settings, Comet::Math::Mat4(1)));
        EXPECT_FALSE(preview.playing());
        const auto updated = preview.snapshot();
        ASSERT_TRUE(updated);
        EXPECT_EQ(updated->instances.front().position_size, first->instances.front().position_size);
        EXPECT_EQ(updated->batches.front().texture, Comet::AssetHandle{12});
        EXPECT_EQ(first->batches.front().texture, Comet::AssetHandle{11});
        EXPECT_EQ(updated->batches.front().blend, Comet::ParticleBlend::Alpha);
        EXPECT_EQ(first->batches.front().blend, Comet::ParticleBlend::Additive);
        EXPECT_FLOAT_EQ(updated->batches.front().soft_distance, 0.4f);
        EXPECT_FLOAT_EQ(first->batches.front().soft_distance, 0);
        EXPECT_EQ(updated->instances.front().uv_rect, (Comet::Math::Vec4{0, 0, 0.5f, 0.5f}));
        EXPECT_EQ(first->instances.front().uv_rect, (Comet::Math::Vec4{0, 0, 1, 1}));
        EXPECT_EQ(updated->instances.front().color, (Comet::Math::Vec4{0, 0, 1, 1}));
        ASSERT_TRUE(preview.step());
        EXPECT_NE(preview.snapshot()->instances.front().position_size,
            updated->instances.front().position_size);
        EXPECT_EQ(preview.snapshot()->instances.front().uv_rect,
            (Comet::Math::Vec4{0.5f, 0, 0.5f, 0.5f}));
        EXPECT_FALSE(preview.playing());
        preview.stop();
        EXPECT_FALSE(preview.snapshot());
    }

    TEST(ParticlePreviewTest, BurstUsesStartupCountOnceAndTargetSwitchDiscardsTheOldPreview) {
        Comet::Scene scene;
        auto a = scene.create_entity(), b = scene.create_entity();
        Comet::ParticleEmitterComponent settings;
        settings.rate = 0;
        settings.burst = 10;
        settings.capacity = 16;
        ParticlePreview preview;
        EXPECT_FALSE(preview.play());
        ASSERT_TRUE(
            preview.bind({scene.get_lifetime(), a.get_uuid()}, settings, Comet::Math::Mat4(1)));
        ASSERT_TRUE(preview.burst());
        ASSERT_EQ(preview.snapshot()->instances.size(), 10u);
        ASSERT_TRUE(preview.burst());
        EXPECT_EQ(preview.snapshot()->instances.size(), 16u);
        const auto previous = preview.snapshot();
        ASSERT_TRUE(
            preview.bind({scene.get_lifetime(), b.get_uuid()}, settings, Comet::Math::Mat4(1)));
        EXPECT_FALSE(preview.playing());
        EXPECT_FALSE(preview.snapshot());
        EXPECT_EQ(previous->instances.size(), 16u);
        settings.enabled = false;
        ASSERT_TRUE(preview.bind(preview.target(), settings, Comet::Math::Mat4(1)));
        EXPECT_FALSE(preview.burst());
        preview.reset();
        EXPECT_FALSE(preview.step());
    }
}
