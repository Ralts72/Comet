#include "animation/animation_system.h"
#include "asset/import/mesh_importer.h"
#include "gameplay/foot_placement.h"
#include "gameplay/pose_constraints.h"
#include "physics/physics_service.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/systems/physics_system.h"
#include <gtest/gtest.h>
#include <algorithm>

namespace Comet::Tests {
    namespace {
        std::shared_ptr<AnimationData> biped() {
            auto data = std::make_shared<AnimationData>();
            data->mesh_node = 0;
            data->nodes = {{"mesh"}, {"body", NO_ANIMATION_NODE, {.translation = {0, 1.4f, 0}}},
                {"hip_l", 1, {.translation = {-.2f, 0, 0}}},
                {"knee_l", 2, {.translation = {0, -.6f, .3f}}},
                {"foot_l", 3, {.translation = {0, -.6f, -.3f}}},
                {"hip_r", 1, {.translation = {.2f, 0, 0}}},
                {"knee_r", 5, {.translation = {0, -.6f, .3f}}},
                {"foot_r", 6, {.translation = {0, -.6f, -.3f}}},
                {"upperarm_l", 1, {.translation = {-.2f, .1f, 0}}},
                {"lowerarm_l", 8, {.translation = {-.5f, 0, 0}}},
                {"hand_l", 9, {.translation = {-.5f, 0, 0}}},
                {"upperarm_r", 1, {.translation = {.2f, .1f, 0}}},
                {"lowerarm_r", 11, {.translation = {.5f, 0, 0}}},
                {"hand_r", 12, {.translation = {.5f, 0, 0}}}};
            for(uint32_t i = 0; i < data->nodes.size(); ++i) {
                data->evaluation_order.push_back(i);
                if(i)
                    data->joints.push_back({i, Math::Mat4(1), BoundingBox::from_point({0, 0, 0})});
            }
            data->clips = {{"idle", 1}};
            return data;
        }
    }
    TEST(FootPlacementTest, QueriesTwoGroundHeightsAndKeepsPhysicsAndFrozenPosesIndependent) {
        const auto data = biped();
        Scene scene;
        auto actor = scene.create_entity("actor");
        auto model = scene.create_entity("model");
        ASSERT_TRUE(scene.set_parent(model, actor));
        model.add_component<MeshRendererComponent>();
        model.add_component<AnimatorComponent>();
        model.add_component<FootPlacementComponent>(
            FootPlacementComponent{.character = actor.get_uuid(),
                .pelvis = "body",
                .pelvis_weight = 1,
                .max_pelvis_offset = .08f});
        auto floor = scene.create_entity("floor");
        floor.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        floor.add_component<ColliderComponent>();
        floor.edit_transform([](auto& value) {
            value.translation.y = -.5f;
            value.scale = {10, 1, 10};
        });
        auto step = scene.create_entity("step");
        step.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        step.add_component<ColliderComponent>();
        step.edit_transform([](auto& value) {
            value.translation = {-.2f, .05f, 0};
            value.scale = {.35f, .1f, .5f};
        });
        auto left_grip = scene.create_entity("left grip"),
             right_grip = scene.create_entity("right grip");
        left_grip.edit_transform([](auto& t) { t.translation = {-.7f, 1.3f, .4f}; });
        right_grip.edit_transform([](auto& t) { t.translation = {.7f, 1.3f, .4f}; });
        model.add_component<HandIKComponent>(HandIKComponent{.left_target = left_grip.get_uuid(),
            .right_target = right_grip.get_uuid(),
            .match_rotation = false});
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::FootPlacementSystem>()));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::PoseConstraintSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto initial = scene.get_animation_playback(model);
        ASSERT_TRUE(initial && initial->pose);
        const auto evaluations = scene.get_animation_statistics().pose_resolutions;
        for(int i = 0; i < 90; ++i) {
            auto advanced = runtime.advance(1.0 / 60);
            ASSERT_TRUE(advanced) << advanced.error().message;
        }
        const auto planted = scene.get_animation_playback(model);
        ASSERT_TRUE(planted && planted->pose);
        ASSERT_EQ(planted->ik.size(), 4u);
        EXPECT_LE(scene.get_animation_statistics().pose_resolutions - evaluations, 92u);
        EXPECT_GT(scene.get_animation_statistics().ik_pose_reuses, 0u);
        EXPECT_NEAR(planted->pose->bone_matrices[3][3].y, .18, .001);
        EXPECT_NEAR(planted->pose->bone_matrices[6][3].y, .08, .001);
        ASSERT_EQ(planted->offsets.size(), 1u);
        EXPECT_NEAR(planted->pose->bone_matrices[0][3].y, 1.32, .001);
        EXPECT_NEAR(initial->pose->bone_matrices[3][3].y, .2, .0001);
        EXPECT_EQ(actor.get_component<TransformComponent>().translation, Math::Vec3(0));
        const auto components = create_scene_component_registry();
        auto text = SceneSerializer(components).serialize(scene);
        ASSERT_TRUE(text);
        auto cloned = SceneSerializer(components).deserialize(text.value());
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_EQ(
            cloned.value()->find_entity(model.get_uuid()).get_component<FootPlacementComponent>(),
            model.get_component<FootPlacementComponent>());
        EXPECT_FALSE(
            cloned.value()->get_animation_playback(cloned.value()->find_entity(model.get_uuid())));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(scene.get_animation_playback(model), planted);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.get_render_animation_playback(model), scene.get_animation_playback(model));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        scene.destroy_entity(step);
        scene.destroy_entity(floor);
        for(int i = 0; i < 90; ++i)
            ASSERT_TRUE(runtime.advance(1.0 / 60));
        EXPECT_EQ(scene.get_animation_playback(model)->ik.size(), 2u);
        for(const auto& ik : scene.get_animation_playback(model)->ik)
            EXPECT_TRUE(ik.tip == 10 || ik.tip == 13);
        EXPECT_NEAR(scene.get_animation_playback(model)->pose->bone_matrices[3][3].y, .2, .0001);
        EXPECT_NEAR(scene.get_animation_playback(model)->pose->bone_matrices[0][3].y, 1.4, .0001);
        EXPECT_NEAR(planted->pose->bone_matrices[3][3].y, .18, .001);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(model));
    }
    TEST(FootPlacementTest, AirborneCharactersAndSwingingFeetReleaseGroundConstraints) {
        const auto data = biped();
        data->clips.push_back({.name = "lifted",
            .duration = 1,
            .channels = {{4, AnimationChannel::Path::Translation,
                AnimationChannel::Interpolation::Linear, {0}, {{0, -.2f, -.3f, 0}}}}});
        Scene scene;
        auto actor = scene.create_entity();
        actor.add_component<CharacterControllerComponent>();
        auto model = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(model, actor));
        model.add_component<MeshRendererComponent>();
        model.add_component<AnimatorComponent>(AnimatorComponent{.transition = 0});
        model.add_component<FootPlacementComponent>(
            FootPlacementComponent{.character = actor.get_uuid(), .right_foot = ""});
        auto floor = scene.create_entity();
        floor.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        floor.add_component<ColliderComponent>();
        floor.edit_transform([](auto& value) {
            value.translation.y = -.5f;
            value.scale = {10, 1, 10};
        });
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::FootPlacementSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 60; ++i)
            ASSERT_TRUE(runtime.advance(1.0 / 60));
        ASSERT_TRUE(physics.get_character_state(actor)->grounded);
        ASSERT_EQ(scene.get_animation_playback(model)->ik.size(), 1u);
        ASSERT_TRUE(physics.request_character_move(actor, {.jump = true}));
        for(int i = 0; i < 35; ++i)
            ASSERT_TRUE(runtime.advance(1.0 / 60));
        EXPECT_FALSE(physics.get_character_state(actor)->grounded);
        EXPECT_TRUE(scene.get_animation_playback(model)->ik.empty());
        for(int i = 0; i < 90; ++i)
            ASSERT_TRUE(runtime.advance(1.0 / 60));
        ASSERT_TRUE(physics.get_character_state(actor)->grounded);
        EXPECT_EQ(scene.get_animation_playback(model)->ik.size(), 1u);
        model.edit_component<AnimatorComponent>([](auto& animator) { animator.clip = "lifted"; });
        for(int i = 0; i < 35; ++i)
            ASSERT_TRUE(runtime.advance(1.0 / 60));
        EXPECT_TRUE(scene.get_animation_playback(model)->ik.empty());
        EXPECT_GT(scene.get_animation_playback(model)->pose->bone_matrices[3][3].y, .5);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(FootPlacementTest, AuthoredDemoUsesBuiltinRigOnStepsAndSlopeWithoutPoseDiagnostics) {
        const auto assets = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets";
        auto mesh = MeshImporter{}.import(
            assets.parent_path().parent_path() / "engine/resources/characters/default.glb");
        ASSERT_TRUE(mesh) << mesh.error();
        const auto data = mesh.value().animation;
        ASSERT_TRUE(data);
        auto scene = SceneSerializer(create_scene_component_registry())
                         .load((assets / "scenes/foot_placement.scene").string());
        ASSERT_TRUE(scene) << scene.error();
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::FootPlacementSystem>()));
        ASSERT_TRUE(runtime.start(*scene.value()));
        for(int i = 0; i < 90; ++i) {
            const auto advanced = runtime.advance(1.0 / 60);
            ASSERT_TRUE(advanced) << advanced.error().message;
        }
        int models = 0;
        scene.value()->each<const FootPlacementComponent>([&](Entity model, const auto& settings) {
            ++models;
            const auto frame = scene.value()->get_animation_playback(model);
            ASSERT_TRUE(frame && frame->pose);
            EXPECT_TRUE(frame->pose_error.empty()) << frame->pose_error;
            ASSERT_EQ(frame->ik.size(), 2u);
            for(const auto& target : frame->ik) {
                const auto found = std::ranges::find(data->joints, target.tip, &SkinJoint::node);
                ASSERT_NE(found, data->joints.end());
                const auto index = std::size_t(found - data->joints.begin());
                EXPECT_LT(
                    Math::length(Math::Vec3(frame->pose->bone_matrices[index][3]) - target.target),
                    .003);
            }
            const auto world = scene.value()->get_world_matrix(model);
            for(const auto& bone_name : {settings.left_foot, settings.right_foot}) {
                const auto node = std::ranges::find(data->nodes, bone_name, &AnimationNode::name);
                ASSERT_NE(node, data->nodes.end());
                const auto joint = std::ranges::find(
                    data->joints, uint32_t(node - data->nodes.begin()), &SkinJoint::node);
                ASSERT_NE(joint, data->joints.end());
                const auto point =
                    Math::Vec3(world * frame->pose->bone_matrices[joint - data->joints.begin()][3]);
                const auto hit = physics.raycast(point + Math::Vec3(0, .5f, 0), {0, -1, 0}, {});
                ASSERT_TRUE(hit && hit.value());
                EXPECT_NEAR(point.y - hit.value()->position.y, settings.foot_height, .004);
            }
        });
        EXPECT_EQ(models, 2);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(FullBodyIKTest, RuntimeCombinesFourGoalsAndPersistsOnlyAuthoringSettings) {
        const auto data = biped();
        Scene scene;
        auto model = scene.create_entity("model");
        model.add_component<MeshRendererComponent>();
        model.add_component<AnimatorComponent>();
        auto left = scene.create_entity("left hand"), right = scene.create_entity("right hand");
        auto left_foot = scene.create_entity("left foot"),
             right_foot = scene.create_entity("right foot");
        left.edit_transform([](auto& t) { t.translation = {-.7f, 1.5f, .8f}; });
        right.edit_transform([](auto& t) { t.translation = {.7f, 1.5f, .8f}; });
        left_foot.edit_transform([](auto& t) { t.translation = {-.2f, .2f, 0}; });
        right_foot.edit_transform([](auto& t) { t.translation = {.2f, .2f, 0}; });
        model.add_component<HandIKComponent>(HandIKComponent{.left_target = left.get_uuid(),
            .right_target = right.get_uuid(),
            .match_rotation = false});
        model.add_component<FullBodyIKComponent>(FullBodyIKComponent{.root = "body",
            .left_target = left_foot.get_uuid(),
            .right_target = right_foot.get_uuid()});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::PoseConstraintSystem>()));
        const auto components = create_scene_component_registry();
        SceneSerializer serializer(components);
        const auto content = serializer.serialize(scene);
        ASSERT_TRUE(content);
        auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        const auto first = scene.get_animation_playback(model);
        ASSERT_TRUE(first && first->pose && first->body_ik);
        EXPECT_EQ(first->ik.size(), 4u);
        ASSERT_TRUE(runtime.advance(1.0 / 30));
        const auto current = scene.get_animation_playback(model);
        ASSERT_TRUE(current && current->pose && current->body_ik);
        EXPECT_TRUE(current->pose_error.empty());
        EXPECT_EQ(model.get_component<TransformComponent>().translation, Math::Vec3(0));
        EXPECT_EQ(serializer.serialize(scene).value(), content.value());
        auto restored = serializer.deserialize(content.value());
        ASSERT_TRUE(restored);
        EXPECT_EQ(
            restored.value()->find_entity(model.get_uuid()).get_component<FullBodyIKComponent>(),
            model.get_component<FullBodyIKComponent>());
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(.2));
        EXPECT_EQ(scene.get_animation_playback(model), current);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_TRUE(scene.get_animation_playback(model)->body_ik);
        EXPECT_EQ(first->ik.size(), 4u);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(model));
    }

    TEST(HandPlacementTest, RejectsNamedNodesOutsideTheSkin) {
        const auto data = biped();
        Scene scene;
        auto model = scene.create_entity("model");
        auto target = scene.create_entity("target");
        model.add_component<MeshRendererComponent>();
        model.add_component<AnimatorComponent>();
        model.add_component<HandIKComponent>(
            HandIKComponent{.left_hand = "mesh", .left_target = target.get_uuid()});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::PoseConstraintSystem>()));
        const auto started = runtime.start(scene);
        ASSERT_FALSE(started);
        EXPECT_NE(started.error().message.find("not part of the skin"), std::string::npos);
        EXPECT_FALSE(scene.get_animation_playback(model));
    }

    TEST(HandPlacementTest, TargetsUseWorldSpaceAndKeepLengthsFrozenPosesAndPause) {
        const auto data = biped();
        Scene scene;
        auto model = scene.create_entity("model");
        model.edit_transform([](auto& t) {
            t.translation = {2, 1, 0};
            t.scale = Math::Vec3(1.5f);
            t.rotation.y = 25;
        });
        model.add_component<MeshRendererComponent>();
        model.add_component<AnimatorComponent>();
        auto left = scene.create_entity("left grip"), right = scene.create_entity("right grip");
        const auto world = scene.get_world_matrix(model);
        const auto move = [&](Entity target, Math::Vec3 position) {
            target.edit_transform(
                [&](auto& t) { t.translation = Math::Vec3(world * Math::Vec4(position, 1)); });
        };
        move(left, {-.7f, 1.3f, .4f});
        move(right, {.7f, 1.3f, .4f});
        model.add_component<HandIKComponent>(HandIKComponent{.left_target = left.get_uuid(),
            .right_target = right.get_uuid(),
            .match_rotation = false});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([data](AssetHandle) { return data; })));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::PoseConstraintSystem>()));
        auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        const auto first = scene.get_animation_playback(model);
        ASSERT_TRUE(first && first->pose);
        ASSERT_EQ(first->ik.size(), 2u);
        EXPECT_LT(Math::length(
                      Math::Vec3(first->pose->bone_matrices[9][3]) - Math::Vec3(-.7f, 1.3f, .4f)),
            .001f);
        const auto from = Math::Vec3(first->pose->bone_matrices[7][3]),
                   middle = Math::Vec3(first->pose->bone_matrices[8][3]),
                   tip = Math::Vec3(first->pose->bone_matrices[9][3]);
        EXPECT_NEAR(Math::length(from - middle), .5f, .0001f);
        EXPECT_NEAR(Math::length(middle - tip), .5f, .0001f);
        move(left, {-.7f, 1.4f, .3f});
        ASSERT_TRUE(runtime.advance(1.0 / 30));
        const auto second = scene.get_animation_playback(model);
        ASSERT_TRUE(second && second->pose);
        EXPECT_NE(second, first);
        EXPECT_NE(first->pose->bone_matrices[9], second->pose->bone_matrices[9]);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_EQ(scene.get_animation_playback(model), second);
        const auto registry = create_scene_component_registry();
        auto saved = SceneSerializer(registry).serialize(scene);
        ASSERT_TRUE(saved);
        auto restored = SceneSerializer(registry).deserialize(saved.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value()->find_entity(model.get_uuid()).get_component<HandIKComponent>(),
            model.get_component<HandIKComponent>());
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(model));
    }

}
