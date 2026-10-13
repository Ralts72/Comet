#include "viewport/physics_overlay.h"
#include "render/debug/line_draw_list.h"
#include "scene/scene_runtime.h"
#include "scene/scene.h"

#include <gtest/gtest.h>
#include <limits>

namespace CometEditor::Tests {
    using namespace Comet;

    TEST(PhysicsOverlayTest, SliderRailUsesConnectedFrameAndDistancesIgnoreScale) {
        Scene scene;
        auto actor = scene.create_entity();
        actor.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {2, 3, 4};
        });
        actor.add_component<RigidBodyComponent>();
        actor.add_component<ColliderComponent>();
        actor.add_component<SliderJointComponent>(
            SliderJointComponent{.connected_anchor = {2, 3, 4},
                .connected_axis = {-1, 0, 0},
                .connected_reference_axis = {0, 1, 0},
                .limits_enabled = true,
                .min_distance = -0.25f,
                .max_distance = 0.75f});
        const auto rail = [&] {
            LineDrawList lines;
            append_joint_anchors(scene, lines);
            std::vector<Math::Vec3> result;
            for(const auto& vertex : lines.vertices()) {
                EXPECT_TRUE(Math::is_finite(vertex.position));
                if(vertex.color == Math::Vec4(1, 1, 0.2f, 1))
                    result.push_back(vertex.position);
            }
            return result;
        };
        const auto world = rail();
        ASSERT_EQ(world.size(), 6u);
        EXPECT_LT(Math::length(world[0] - Math::Vec3(2.25f, 3, 4)), 1e-5f);
        EXPECT_LT(Math::length(world[1] - Math::Vec3(1.25f, 3, 4)), 1e-5f);
        auto target = scene.create_entity();
        target.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {3, 4, 5};
        });
        target.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        target.add_component<ColliderComponent>();
        actor.edit_component<SliderJointComponent>([&](auto& joint) {
            joint.connected_body = target.get_uuid();
            joint.connected_anchor = {};
            joint.connected_axis = {0, 1, 0};
            joint.connected_reference_axis = {1, 0, 0};
        });
        const auto local = rail();
        ASSERT_EQ(local.size(), world.size());
        for(std::size_t i = 0; i < local.size(); ++i)
            EXPECT_LT(Math::length(local[i] - world[i]), 1e-5f);
        actor.edit_component<SliderJointComponent>([](auto& joint) { joint.min_distance = 1; });
        EXPECT_TRUE(rail().empty());
        actor.edit_component<SliderJointComponent>(
            [](auto& joint) { joint.limits_enabled = false; });
        EXPECT_TRUE(rail().empty());
    }

    TEST(PhysicsOverlayTest, OutlinesUseColliderDimensionsWithoutRequiringMeshes) {
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

    TEST(PhysicsOverlayTest, CharacterOutlineUsesFootOriginAndLocalCapsuleDimensions) {
        Scene scene;
        auto character = scene.create_entity();
        character.add_component<CharacterControllerComponent>();
        character.edit_transform([](auto& t) {
            t.translation = {2, 3, 4};
            t.scale = Math::Vec3(2);
        });
        LineDrawList lines;
        append_collider_outlines(scene, lines);
        ASSERT_EQ(lines.line_count(), 132u);
        float minimum = std::numeric_limits<float>::max();
        float maximum = std::numeric_limits<float>::lowest();
        for(const auto& vertex : lines.vertices()) {
            minimum = std::min(minimum, vertex.position.y);
            maximum = std::max(maximum, vertex.position.y);
        }
        EXPECT_NEAR(minimum, 3, 1e-5f);
        EXPECT_NEAR(maximum, 6.6f, 1e-5f);
        character.edit_component<CharacterControllerComponent>([](auto& c) { c.enabled = false; });
        lines.clear();
        append_collider_outlines(scene, lines);
        EXPECT_EQ(lines.vertices().front().color, Math::Vec4(0.6f, 0.6f, 0.6f, 1));
    }

    TEST(PhysicsOverlayTest, ColorsDistinguishBodyKindsAndUnsupportedConfiguration) {
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

    TEST(PhysicsOverlayTest, WorldJointShowsScaledRotatedLocalAnchorAndPositionGap) {
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<RigidBodyComponent>();
        entity.add_component<ColliderComponent>();
        entity.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {2, 1, 0.5f};
        });
        entity.add_component<PointJointComponent>(
            PointJointComponent{.anchor = {1, 0, 0}, .connected_anchor = {3, 5, 4}});
        LineDrawList lines;
        append_joint_anchors(scene, lines);
        ASSERT_EQ(lines.line_count(), 17u);
        const auto vertices = lines.vertices();
        const auto gap = vertices.subspan(vertices.size() - 2);
        EXPECT_LT(Math::length(gap[0].position - Math::Vec3(2, 5, 4)), 1e-5f);
        EXPECT_EQ(gap[1].position, Math::Vec3(3, 5, 4));
        EXPECT_EQ(gap[0].color, Math::Vec4(1, 1, 0.2f, 1));
        EXPECT_EQ(vertices.front().color, Math::Vec4(1, 0.65f, 0.1f, 1));
        EXPECT_EQ(vertices[8].color, Math::Vec4(0.1f, 0.9f, 0.9f, 1));
        EXPECT_EQ(
            entity.get_component<PointJointComponent>().connected_anchor, Math::Vec3(3, 5, 4));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(2, 3, 4));
    }

    TEST(PhysicsOverlayTest,
        ConnectedAnchorFollowsTargetAndMissingReferenceDoesNotBecomeWorldPoint) {
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<RigidBodyComponent>();
        entity.add_component<ColliderComponent>();
        auto target = scene.create_entity();
        target.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        target.add_component<ColliderComponent>();
        target.edit_transform([](auto& value) {
            value.translation = {5, 6, 7};
            value.rotation.z = 90;
            value.scale = {2, 3, 4};
        });
        entity.add_component<PointJointComponent>(PointJointComponent{
            .connected_body = target.get_uuid(), .connected_anchor = {1, 0, 0}});
        LineDrawList lines;
        append_joint_anchors(scene, lines);
        ASSERT_EQ(lines.line_count(), 18u);
        EXPECT_LT(Math::length(lines.vertices().back().position - Math::Vec3(5, 8, 7)), 1e-5f);
        target.edit_transform([](auto& value) { value.translation.x += 2; });
        lines.clear();
        append_joint_anchors(scene, lines);
        EXPECT_LT(Math::length(lines.vertices().back().position - Math::Vec3(7, 8, 7)), 1e-5f);
        const auto reference = target.get_uuid();
        scene.destroy_entity(target);
        scene.create_entity("Replacement");
        lines.clear();
        append_joint_anchors(scene, lines);
        ASSERT_EQ(lines.line_count(), 4u);
        for(const auto& vertex : lines.vertices())
            EXPECT_EQ(vertex.color, Math::Vec4(1, 0.2f, 0.2f, 1));
        EXPECT_EQ(entity.get_component<PointJointComponent>().connected_body, reference);
    }

    TEST(PhysicsOverlayTest, DisabledAndUnsupportedJointsAreMarkedWithoutInvalidVertices) {
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<RigidBodyComponent>();
        entity.add_component<ColliderComponent>();
        entity.add_component<PointJointComponent>(
            PointJointComponent{.connected_anchor = {0, 2, 0}});
        const auto expect_color = [&](Math::Vec4 color) {
            LineDrawList lines;
            append_joint_anchors(scene, lines);
            ASSERT_FALSE(lines.empty());
            for(const auto& vertex : lines.vertices())
                EXPECT_EQ(vertex.color, color);
        };
        entity.edit_component<PointJointComponent>([](auto& value) { value.enabled = false; });
        expect_color({0.6f, 0.6f, 0.6f, 1});
        entity.edit_component<PointJointComponent>([](auto& value) { value.enabled = true; });
        entity.edit_component<RigidBodyComponent>(
            [](auto& value) { value.motion = BodyMotion::Static; });
        expect_color({1, 0.2f, 0.2f, 1});
        entity.edit_component<RigidBodyComponent>(
            [](auto& value) { value.motion = BodyMotion::Dynamic; });
        entity.edit_component<PointJointComponent>(
            [&](auto& value) { value.connected_body = entity.get_uuid(); });
        expect_color({1, 0.2f, 0.2f, 1});
        entity.edit_component<PointJointComponent>([](auto& value) { value.connected_body = {}; });
        ASSERT_TRUE(scene.set_parent(entity, scene.create_entity()));
        expect_color({1, 0.2f, 0.2f, 1});
        ASSERT_TRUE(scene.clear_parent(entity));
        entity.remove_component<ColliderComponent>();
        expect_color({1, 0.2f, 0.2f, 1});
        entity.edit_component<PointJointComponent>([](auto& value) {
            value.connected_anchor.x = std::numeric_limits<float>::quiet_NaN();
        });
        LineDrawList lines;
        append_joint_anchors(scene, lines);
        for(const auto& vertex : lines.vertices())
            EXPECT_TRUE(Math::is_finite(vertex.position));
        entity.edit_component<PointJointComponent>(
            [](auto& value) { value.anchor.x = std::numeric_limits<float>::quiet_NaN(); });
        lines.clear();
        append_joint_anchors(scene, lines);
        EXPECT_TRUE(lines.empty());
    }

    TEST(PhysicsOverlayTest, HingeAxesUseUnscaledRotationsAndInvalidDirectionsAreMarked) {
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<RigidBodyComponent>();
        entity.add_component<ColliderComponent>();
        entity.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {2, 1, 0.5f};
        });
        auto target = scene.create_entity();
        target.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        target.add_component<ColliderComponent>();
        target.edit_transform([](auto& value) {
            value.translation = {3, 5, 4};
            value.rotation.z = -90;
            value.scale = {3, 1, 6};
        });
        entity.add_component<HingeJointComponent>(
            HingeJointComponent{.connected_body = target.get_uuid(),
                .anchor = {1, 0, 0},
                .axis = {1, 1, 0},
                .connected_axis = {1, 1, 0}});
        LineDrawList lines;
        append_joint_anchors(scene, lines);
        ASSERT_GE(lines.vertices().size(), 6u);
        const auto axes = lines.vertices().last(6).first(4);
        EXPECT_LT(Math::length((axes[0].position + axes[1].position) * 0.5f - Math::Vec3(2, 5, 4)),
            1e-5f);
        EXPECT_LT(Math::length((axes[2].position + axes[3].position) * 0.5f - Math::Vec3(3, 5, 4)),
            1e-5f);
        EXPECT_LT(Math::length((axes[1].position - axes[0].position)
                               - Math::normalize(Math::Vec3(-1, 1, 0)) * 0.36f),
            1e-5f);
        EXPECT_LT(Math::length((axes[3].position - axes[2].position)
                               - Math::normalize(Math::Vec3(1, -1, 0)) * 0.36f),
            1e-5f);
        entity.edit_component<HingeJointComponent>([](auto& joint) {
            joint.connected_body = {};
            joint.connected_anchor = {3, 5, 4};
        });
        lines.clear();
        append_joint_anchors(scene, lines);
        const auto world_axis = lines.vertices().last(4).first(2);
        EXPECT_LT(Math::length((world_axis[1].position - world_axis[0].position)
                               - Math::normalize(Math::Vec3(1, 1, 0)) * 0.36f),
            1e-5f);
        for(const auto direction :
            {Math::Vec3(0), Math::Vec3(0, 0, std::numeric_limits<float>::infinity())}) {
            entity.edit_component<HingeJointComponent>(
                [&](auto& joint) { joint.axis = direction; });
            lines.clear();
            append_joint_anchors(scene, lines);
            ASSERT_FALSE(lines.empty());
            for(const auto& vertex : lines.vertices()) {
                EXPECT_TRUE(Math::is_finite(vertex.position));
                EXPECT_EQ(vertex.color, Math::Vec4(1, 0.2f, 0.2f, 1));
            }
        }
    }

    TEST(PhysicsOverlayTest, HingeLimitArcUsesConnectedZeroDirectionAndUnscaledPose) {
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<RigidBodyComponent>();
        entity.add_component<ColliderComponent>();
        entity.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {2, 3, 4};
        });
        entity.add_component<HingeJointComponent>(HingeJointComponent{.connected_anchor = {2, 3, 4},
            .limits_enabled = true,
            .connected_reference_axis = {0, 1, 2},
            .min_angle = -30,
            .max_angle = 60});
        const auto arc = [&] {
            LineDrawList lines;
            append_joint_anchors(scene, lines);
            std::vector<Math::Vec3> points;
            for(const auto& vertex : lines.vertices()) {
                if(vertex.color == Math::Vec4(1, 1, 0.2f, 1))
                    points.push_back(vertex.position);
                EXPECT_TRUE(Math::is_finite(vertex.position));
            }
            return points;
        };
        const auto world_arc = arc();
        ASSERT_EQ(world_arc.size(), 68u);
        EXPECT_EQ(world_arc.front(), Math::Vec3(2, 3, 4));
        EXPECT_LT(Math::length(world_arc[1]
                               - (Math::Vec3(2, 3, 4)
                                   + Math::Vec3(0.5f, std::sqrt(3.0f) * 0.5f, 0) * 0.16f)),
            1e-5f);
        EXPECT_LT(Math::length(world_arc.back()
                               - (Math::Vec3(2, 3, 4)
                                   + Math::Vec3(-std::sqrt(3.0f) * 0.5f, 0.5f, 0) * 0.16f)),
            1e-5f);
        auto target = scene.create_entity();
        target.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        target.add_component<ColliderComponent>();
        target.edit_transform([](auto& value) {
            value.translation = {2, 3, 4};
            value.rotation.z = 90;
            value.scale = {3, 4, 5};
        });
        entity.edit_component<HingeJointComponent>([&](auto& joint) {
            joint.connected_body = target.get_uuid();
            joint.connected_anchor = {};
            joint.connected_reference_axis = {1, 0, 2};
        });
        const auto local_arc = arc();
        ASSERT_EQ(local_arc.size(), world_arc.size());
        for(std::size_t i = 0; i < local_arc.size(); ++i)
            EXPECT_LT(Math::length(local_arc[i] - world_arc[i]), 1e-5f);
        entity.edit_component<HingeJointComponent>([](auto& joint) { joint.min_angle = 1; });
        EXPECT_TRUE(arc().empty());
    }

    TEST(PhysicsOverlayTest, PositionTargetsUseConnectedFrameClampToLimitsAndRespectDisabledColor) {
        const auto check = []<typename Joint>() {
            Scene scene;
            auto actor = scene.create_entity();
            actor.add_component<RigidBodyComponent>();
            actor.add_component<ColliderComponent>();
            auto target = scene.create_entity();
            target.add_component<RigidBodyComponent>(
                RigidBodyComponent{.motion = BodyMotion::Static});
            target.add_component<ColliderComponent>();
            target.edit_transform([](auto& pose) {
                pose.translation = {2, 3, 4};
                pose.rotation.z = 90;
                pose.scale = {3, 4, 5};
            });
            Joint settings;
            settings.connected_body = target.get_uuid();
            settings.motor_enabled = true;
            settings.motor_mode = JointMotorMode::Position;
            if constexpr(std::is_same_v<Joint, HingeJointComponent>) {
                settings.motor_target = 30;
                settings.min_angle = -15;
                settings.max_angle = 20;
            } else {
                settings.motor_target = 0.4f;
                settings.min_distance = -0.2f;
                settings.max_distance = 0.25f;
            }
            actor.add_component<Joint>(settings);
            const auto marker_center = [&] {
                LineDrawList lines;
                append_joint_anchors(scene, lines);
                std::vector<Math::Vec3> marker;
                for(const auto& vertex : lines.vertices()) {
                    EXPECT_TRUE(Math::is_finite(vertex.position));
                    if(vertex.color == Math::Vec4(0.4f, 1, 0.4f, 1))
                        marker.push_back(vertex.position);
                }
                EXPECT_EQ(marker.size(), 2u);
                if(marker.size() != 2)
                    return Math::Vec3{};
                if constexpr(std::is_same_v<Joint, HingeJointComponent>)
                    return marker.back();
                return (marker.front() + marker.back()) * 0.5f;
            };
            const auto expected = [](float target_value) {
                if constexpr(std::is_same_v<Joint, HingeJointComponent>)
                    return Math::Vec3(2, 3, 4)
                           + Math::Vec3(-std::sin(Math::radians(target_value)),
                                 std::cos(Math::radians(target_value)), 0)
                                 * 0.16f;
                return Math::Vec3(2 - target_value, 3, 4);
            };
            EXPECT_LT(Math::length(marker_center() - expected(settings.motor_target)), 1e-5f);
            settings.limits_enabled = true;
            ASSERT_TRUE(actor.try_set_component(settings));
            float maximum;
            if constexpr(std::is_same_v<Joint, HingeJointComponent>)
                maximum = settings.max_angle;
            else
                maximum = settings.max_distance;
            EXPECT_LT(Math::length(marker_center() - expected(maximum)), 1e-5f);
            actor.edit_component<Joint>([](auto& joint) { joint.enabled = false; });
            LineDrawList lines;
            append_joint_anchors(scene, lines);
            for(const auto& vertex : lines.vertices())
                EXPECT_EQ(vertex.color, Math::Vec4(0.6f, 0.6f, 0.6f, 1));
            actor.edit_component<Joint>([](auto& joint) {
                joint.enabled = true;
                joint.reference_axis = joint.axis;
            });
            lines.clear();
            append_joint_anchors(scene, lines);
            for(const auto& vertex : lines.vertices())
                EXPECT_EQ(vertex.color, Math::Vec4(1, 0.2f, 0.2f, 1));
        };
        check.template operator()<HingeJointComponent>();
        check.template operator()<SliderJointComponent>();
    }

    TEST(PhysicsOverlayTest, HingeAxisPreviewUsesInterpolatedRotation) {
        class Rotate final: public System {
        public:
            Result<void, Error> fixed_update(Scene& scene, const Context&) override {
                scene.each<const HingeJointComponent>(
                    [](Entity entity, const HingeJointComponent&) {
                        entity.edit_transform([](auto& value) { value.rotation.z += 90; });
                    });
                return Result<void, Error>::success();
            }
        };
        Scene scene;
        SceneRuntime runtime;
        auto entity = scene.create_entity();
        entity.add_component<RigidBodyComponent>();
        entity.add_component<ColliderComponent>();
        entity.add_component<HingeJointComponent>(HingeJointComponent{.axis = {1, 0, 0}});
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Rotate>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        LineDrawList lines;
        append_joint_anchors(scene, lines);
        const auto axes = lines.vertices().last(4);
        EXPECT_LT(Math::length((axes[1].position - axes[0].position)
                               - Math::normalize(Math::Vec3(1, 1, 0)) * 0.36f),
            1e-5f);
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().rotation.z, 90);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(PhysicsOverlayTest, CaptureUsesInterpolatedPoseAndPauseStepWithoutMutatingSceneState) {
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
        entity.add_component<RigidBodyComponent>();
        entity.add_component<PointJointComponent>();
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Move>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.15));
        LineDrawList lines;
        append_collider_outlines(scene, lines);
        ASSERT_EQ(lines.line_count(), 12u);
        EXPECT_NEAR(lines.vertices()[0].position.x, 4.5f, 1e-5f);
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 10);
        LineDrawList joints;
        append_joint_anchors(scene, joints);
        ASSERT_FALSE(joints.empty());
        EXPECT_NEAR(
            (joints.vertices()[0].position.x + joints.vertices()[1].position.x) * 0.5f, 5, 1e-5f);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        lines.clear();
        append_collider_outlines(scene, lines);
        EXPECT_FLOAT_EQ(lines.vertices()[0].position.x, 19.5f);
        joints.clear();
        append_joint_anchors(scene, joints);
        EXPECT_FLOAT_EQ(
            (joints.vertices()[0].position.x + joints.vertices()[1].position.x) * 0.5f, 20);
        ASSERT_TRUE(runtime.stop());
        scene.destroy_entity(entity);
        lines.clear();
        append_collider_outlines(scene, lines);
        EXPECT_TRUE(lines.empty());
        joints.clear();
        append_joint_anchors(scene, joints);
        EXPECT_TRUE(joints.empty());
    }
}
