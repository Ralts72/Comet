#include "viewport/collider_overlay.h"
#include "render/debug/line_draw_list.h"
#include "scene/scene_runtime.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    using namespace Comet;

    TEST(ColliderOverlayTest, OutlinesUseColliderDimensionsWithoutRequiringMeshes) {
        Scene scene;
        auto box = scene.create_entity();
        box.add_component<ColliderComponent>(ColliderComponent{.half_extents = {1, 2, 3}});
        box.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation = {0, 0, 90};
            value.scale = {2, 1, 0.5f};
        });
        LineDrawList lines;
        append_collider_outlines(scene, lines);
        ASSERT_EQ(lines.line_count(), 12u);
        EXPECT_LT(Math::length(lines.vertices()[0].position - Math::Vec3(4, 1, 2.5f)), 1e-5f);
        EXPECT_EQ(lines.vertices()[0].color, Math::Vec4(0.6f, 0.6f, 0.6f, 1));
        box.edit_component<ColliderComponent>([](auto& value) {
            value.shape = ColliderShape::Sphere;
            value.radius = 2;
        });
        box.edit_transform([](auto& value) { value.scale = Math::Vec3(3); });
        lines.clear();
        append_collider_outlines(scene, lines);
        ASSERT_EQ(lines.line_count(), 96u);
        for(const auto& vertex : lines.vertices())
            EXPECT_NEAR(Math::length(vertex.position - Math::Vec3(2, 3, 4)), 6, 1e-5f);
    }

    TEST(ColliderOverlayTest, ColorsDistinguishBodyKindsAndUnsupportedConfiguration) {
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<ColliderComponent>();
        entity.add_component<RigidBodyComponent>();
        const auto color = [&] {
            LineDrawList lines;
            append_collider_outlines(scene, lines);
            return lines.vertices().front().color;
        };
        for(const auto& [motion, expected] :
            {std::pair{BodyMotion::Static, Math::Vec4(0.25f, 0.55f, 1, 1)},
                std::pair{BodyMotion::Dynamic, Math::Vec4(0.2f, 1, 0.3f, 1)},
                std::pair{BodyMotion::Kinematic, Math::Vec4(0.8f, 0.4f, 1, 1)}}) {
            entity.edit_component<RigidBodyComponent>([&](auto& value) { value.motion = motion; });
            EXPECT_EQ(color(), expected);
        }
        entity.edit_component<ColliderComponent>([](auto& value) { value.is_trigger = true; });
        EXPECT_EQ(color(), Math::Vec4(0.1f, 0.9f, 0.9f, 1));
        const auto parent = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(entity, parent));
        EXPECT_EQ(color(), Math::Vec4(1, 0.2f, 0.2f, 1));
        ASSERT_TRUE(scene.clear_parent(entity));
        entity.edit_component<ColliderComponent>(
            [](auto& value) { value.shape = ColliderShape::Sphere; });
        entity.edit_transform([](auto& value) { value.scale = {1, 2, 1}; });
        EXPECT_EQ(color(), Math::Vec4(1, 0.2f, 0.2f, 1));
        entity.edit_transform([](auto& value) { value.scale = Math::Vec3(-1); });
        EXPECT_EQ(color(), Math::Vec4(1, 0.2f, 0.2f, 1));
    }

    TEST(ColliderOverlayTest, CaptureUsesInterpolatedPoseAndPauseStepWithoutMutatingPhysicsState) {
        class Move final: public System {
        public:
            Result<void, Error> fixed_update(Scene& scene, const Context&) override {
                scene.each<const ColliderComponent>([](Entity entity, const ColliderComponent&) {
                    entity.edit_transform([](auto& value) { value.translation.x += 10; });
                });
                return Result<void, Error>::success();
            }
        };
        Scene scene;
        SceneRuntime runtime;
        auto entity = scene.create_entity();
        entity.add_component<ColliderComponent>();
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Move>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        LineDrawList lines;
        append_collider_outlines(scene, lines);
        ASSERT_EQ(lines.line_count(), 12u);
        EXPECT_NEAR(lines.vertices()[0].position.x, 4.5f, 1e-5f);
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 10);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        lines.clear();
        append_collider_outlines(scene, lines);
        EXPECT_FLOAT_EQ(lines.vertices()[0].position.x, 19.5f);
        ASSERT_TRUE(runtime.stop());
        scene.destroy_entity(entity);
        lines.clear();
        append_collider_outlines(scene, lines);
        EXPECT_TRUE(lines.empty());
    }
}
