#include <gtest/gtest.h>
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "render/scene/scene_extractor.h"
#include "support/math_assertions.h"

#include <functional>
#include <utility>

namespace Comet::Tests {
    namespace {
        class FixedMotion final: public System {
        public:
            explicit FixedMotion(Entity entity) : target(entity) {}
            Result<void, Error> fixed_update(Scene&, const Context&) override {
                if(moving) {
                    auto transform = target.get_component<TransformComponent>();
                    transform.translation.x += 10;
                    if(next)
                        transform = *std::exchange(next, {});
                    target.set_transform(transform, teleport ? Entity::TransformUpdate::Teleport
                                                             : Entity::TransformUpdate::Continuous);
                }
                if(after_fixed)
                    after_fixed();
                return Result<void, Error>::success();
            }
            Result<void, Error> update(Scene&, const Context&) override {
                if(ordinary)
                    ordinary();
                return Result<void, Error>::success();
            }
            Entity target;
            bool moving = true, teleport = false;
            std::optional<TransformComponent> next;
            std::function<void()> after_fixed, ordinary;
        };

        class TransformInterpolationTest: public testing::Test {
        protected:
            Scene scene;
            SceneRuntime runtime;
            SceneExtractor extractor;
            Entity actor;
            FixedMotion* motion = nullptr;
            void SetUp() override {
                actor = scene.create_entity();
                actor.add_component<MeshRendererComponent>();
                ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1}));
                auto system = std::make_unique<FixedMotion>(actor);
                motion = system.get();
                ASSERT_TRUE(runtime.add_system(std::move(system)));
            }
            float render_x(Entity entity) {
                return scene.get_render_transform(entity).world_matrix[3].x;
            }
        };
    }

    TEST_F(
        TransformInterpolationTest, ZeroStepFramesBlendRotationScaleAndKeepTrueStateAndOldFrames) {
        actor.edit_transform([](auto& transform) { transform.rotation.y = 179; });
        motion->next = TransformComponent{
            .translation = {10, 0, 0}, .rotation = {0, -179, 0}, .scale = Math::Vec3(3)};
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 1u);
        const auto first = extractor.update(scene);
        ASSERT_EQ(first->render_items.size(), 1u);
        const auto expected = Math::compose_trs({5, 0, 0}, {0, 180, 0}, Math::Vec3(2));
        EXPECT_TRUE(TestUtils::Mat4Equal(first->render_items.front().model_matrix, expected));
        EXPECT_FLOAT_EQ(scene.get_world_matrix(actor)[3].x, 10);
        EXPECT_EQ(actor.get_component<TransformComponent>().scale, Math::Vec3(3));
        const auto world_revision = actor.get_component<WorldTransformComponent>().revision;
        ASSERT_TRUE(runtime.advance(0.025));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 0u);
        const auto next = extractor.update(scene);
        EXPECT_FLOAT_EQ(next->render_items.front().model_matrix[3].x, 7.5f);
        EXPECT_NE(first->render_items.front().transform_revision,
            next->render_items.front().transform_revision);
        EXPECT_EQ(actor.get_component<WorldTransformComponent>().revision, world_revision);
        EXPECT_TRUE(TestUtils::Mat4Equal(first->render_items.front().model_matrix, expected));
        EXPECT_EQ(extractor.update(scene), next);
    }

    TEST_F(TransformInterpolationTest, MeshCameraAndLightShareInterpolatedAncestorAndUnscaledPose) {
        auto child = scene.create_entity();
        child.edit_transform([](auto& transform) { transform.translation.x = 2; });
        child.add_component<MeshRendererComponent>();
        child.add_component<CameraComponent>();
        child.add_component<LightComponent>();
        ASSERT_TRUE(scene.set_parent(child, actor));
        motion->next = TransformComponent{.translation = {10, 0, 0}, .scale = Math::Vec3(2)};
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        const auto frame = extractor.update(scene);
        ASSERT_EQ(frame->cameras.size(), 1u);
        ASSERT_EQ(frame->lights.size(), 1u);
        EXPECT_FLOAT_EQ(render_x(actor), 5);
        EXPECT_FLOAT_EQ(render_x(child), 8);
        EXPECT_FLOAT_EQ(scene.get_world_matrix(child)[3].x, 14);
        EXPECT_FLOAT_EQ(frame->lights.front().position.x, 8);
        EXPECT_FLOAT_EQ(Math::inverse(frame->cameras.front().view_matrix)[3].x, 8);
        EXPECT_NEAR(glm::length(Math::Vec3(Math::inverse(frame->cameras.front().view_matrix)[0])),
            1.0f, 1e-6f);
        auto attached = scene.create_entity();
        attached.add_component<MeshRendererComponent>();
        attached.edit_transform([](auto& transform) { transform.translation.x = 4; });
        ASSERT_TRUE(scene.set_parent(attached, actor));
        EXPECT_FLOAT_EQ(render_x(attached), 11);
        ASSERT_TRUE(runtime.advance(0.025));
        EXPECT_FLOAT_EQ(render_x(attached), 14.5f);
    }

    TEST_F(TransformInterpolationTest, CatchUpStepsRetainTheLastPairAndRestingMotionSettles) {
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.25));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 2u);
        EXPECT_FLOAT_EQ(render_x(actor), 15);
        motion->moving = false;
        ASSERT_TRUE(runtime.advance(0.05));
        EXPECT_FLOAT_EQ(render_x(actor), 20);
        const auto idle = extractor.update(scene);
        ASSERT_TRUE(runtime.advance(0.03));
        EXPECT_EQ(extractor.update(scene), idle);
    }

    TEST_F(TransformInterpolationTest, PauseSingleStepResumeAndStopPresentCurrentPose) {
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        EXPECT_FLOAT_EQ(render_x(actor), 5);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        EXPECT_FLOAT_EQ(render_x(actor), 10);
        ASSERT_TRUE(runtime.advance(0.5));
        EXPECT_FLOAT_EQ(render_x(actor), 10);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(render_x(actor), 20);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(0.025));
        EXPECT_FLOAT_EQ(render_x(actor), 20);
        ASSERT_TRUE(runtime.advance(0.125));
        EXPECT_FLOAT_EQ(render_x(actor), 25);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FLOAT_EQ(render_x(actor), 30);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.05));
        EXPECT_FLOAT_EQ(render_x(actor), 30);
    }

    TEST_F(TransformInterpolationTest, OrdinaryUpdatesAndExplicitFixedTeleportSnapImmediately) {
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        EXPECT_FLOAT_EQ(render_x(actor), 5);
        motion->ordinary = [&] {
            actor.edit_transform([](auto& transform) { transform.translation.x = 100; });
        };
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(render_x(actor), 100);
        motion->ordinary = {};
        motion->teleport = true;
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(render_x(actor), 110);
        motion->teleport = false;
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(render_x(actor), 115);
        const auto unchanged = actor.get_component<TransformComponent>();
        ASSERT_TRUE(actor.try_set_transform(unchanged, Entity::TransformUpdate::Teleport));
        EXPECT_FLOAT_EQ(render_x(actor), 120);
    }

    TEST_F(TransformInterpolationTest, SpawnReparentAndRecycledEntitiesDoNotUseOldHistory) {
        auto parent = scene.create_entity();
        parent.edit_transform([](auto& transform) { transform.translation.x = 100; });
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        EXPECT_FLOAT_EQ(render_x(actor), 5);
        ASSERT_TRUE(scene.set_parent(actor, parent));
        EXPECT_FLOAT_EQ(render_x(actor), 110);
        Entity spawned;
        motion->after_fixed = [&] {
            spawned = scene.create_entity();
            spawned.add_component<MeshRendererComponent>();
            spawned.edit_transform([](auto& transform) { transform.translation.x = 200; });
        };
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(render_x(spawned), 200);
        scene.destroy_entity(actor);
        motion->moving = false;
        auto replacement = scene.create_entity();
        replacement.add_component<MeshRendererComponent>();
        replacement.edit_transform([](auto& transform) { transform.translation.x = 300; });
        EXPECT_FLOAT_EQ(render_x(replacement), 300);
        EXPECT_EQ(extractor.update(scene)->render_items.size(), 2u);
    }
}
