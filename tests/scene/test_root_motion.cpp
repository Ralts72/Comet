#include "animation/animation_data.h"
#include "animation/animation_system.h"
#include "gameplay/character_animation.h"
#include "gameplay/root_motion.h"
#include "asset/import/mesh_importer.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "physics/physics_service.h"
#include "render/scene/scene_extractor.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/systems/physics_system.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        AnimationData motion_data() {
            AnimationData data;
            data.mesh_node = 0;
            data.nodes = {{"Mesh"}, {"Root"}};
            data.evaluation_order = {0, 1};
            data.joints = {{.node = 1, .bounds = BoundingBox::from_point({0, 0, 0})}};
            data.clips = {{.name = "move",
                .duration = 1,
                .channels = {{.node = 1,
                    .path = AnimationChannel::Path::Translation,
                    .interpolation = AnimationChannel::Interpolation::Linear,
                    .times = {0, 1},
                    .values = {{0, 0, 0, 0}, {2, 1, 0, 0}}}}}};
            return data;
        }
        Entity add_model(Scene& scene, EntityUuid target = {}) {
            auto model = scene.create_entity("model");
            model.add_component<MeshRendererComponent>(AssetHandle(1), AssetHandle(2));
            model.add_component<AnimatorComponent>(
                AnimatorComponent{.clip = "move", .transition = 0});
            model.add_component<RootMotionComponent>(RootMotionComponent{.target = target});
            return model;
        }
        class OppositeIntent final: public System {
        public:
            Entity actor;
            Result<void, Error> fixed_update(Scene&, const Context& context) override {
                if(!actor.get_component<CharacterControllerComponent>().enabled)
                    return Result<void, Error>::success();
                if(!context.services.physics->request_character_move(actor, {.direction = {-1, 0}}))
                    return Result<void, Error>::failure({"Cannot submit test movement"});
                return Result<void, Error>::success();
            }
        };
        std::unique_ptr<AnimationSystem> root_system(std::shared_ptr<const AnimationData> data,
            AnimationSystem::Controllers controllers = {}) {
            return std::make_unique<AnimationSystem>([data](AssetHandle) { return data; },
                std::move(controllers), AnimationSystem::Libraries{}, Gameplay::apply_root_motion);
        }
    }

    TEST(RootMotionTest, UnwrapsMultipleCyclesClampsAndKeepsUnselectedVerticalAnimation) {
        const auto data = motion_data();
        ASSERT_TRUE(validate_animation(data));
        AnimationBlend sample{.clip = "move", .time = .75};
        auto motion = animation_root_motion(data, sample, 3.25, {});
        ASSERT_TRUE(motion) << motion.error();
        EXPECT_NEAR(motion.value().translation.x, 5, .0001);
        EXPECT_NEAR(motion.value().translation.y, 0, .0001);
        sample.normalized_time = true;
        auto phase = animation_root_motion(data, sample, 3.25, {.vertical = true});
        ASSERT_TRUE(phase);
        EXPECT_NEAR(phase.value().translation.y, 2.5, .0001);
        sample.loop = false;
        auto stopped = animation_root_motion(data, sample, 3.25, {});
        ASSERT_TRUE(stopped);
        EXPECT_NEAR(stopped.value().translation.x, .5, .0001);
        AnimationPlayback frame{
            .current = {.clip = "move", .time = .5}, .root_motion = AnimationRootMotion{}};
        auto pose = sample_animation(data, &frame);
        ASSERT_TRUE(pose) << pose.error();
        EXPECT_NEAR(pose.value().bone_matrices[0][3].x, 0, .0001);
        EXPECT_NEAR(pose.value().bone_matrices[0][3].y, .5, .0001);
        frame.root_motion->vertical = true;
        EXPECT_NEAR(sample_animation(data, &frame).value().bone_matrices[0][3].y, 0, .0001);
        EXPECT_FALSE(animation_root_motion(data, sample, .5, {}));
        EXPECT_FALSE(animation_root_motion(data, sample, 1, {.node = "missing"}));
    }

    TEST(RootMotionTest, TurningCyclesComposeAndMeshImportBasisDoesNotChangeWorldDisplacement) {
        auto data = motion_data();
        auto& clip = data.clips[0];
        clip.channels[0].values = {{0, 0, 0, 0}, {0, 0, 2, 0}};
        const auto q = Math::angle_axis(Math::PI / 2, {0, 1, 0});
        clip.channels.push_back({.node = 1,
            .path = AnimationChannel::Path::Rotation,
            .interpolation = AnimationChannel::Interpolation::Linear,
            .times = {0, 1},
            .values = {{0, 0, 0, 1}, {q.x, q.y, q.z, q.w}}});
        auto motion = animation_root_motion(data, {.clip = "move"}, 2, {});
        ASSERT_TRUE(motion) << motion.error();
        EXPECT_NEAR(motion.value().translation.x, 2, .0001);
        EXPECT_NEAR(motion.value().translation.z, 2, .0001);
        EXPECT_NEAR(std::abs(motion.value().yaw), Math::PI, .0001);
        data.nodes[0].rest.rotation = Math::angle_axis(Math::PI / 2, {1, 0, 0});
        data.nodes[0].rest.scale = {2, 2, 2};
        auto rotated = animation_root_motion(data, {.clip = "move"}, 2, {});
        ASSERT_TRUE(rotated) << rotated.error();
        const auto basis = glm::mat4_cast(data.nodes[0].rest.rotation)
                           * Math::scale(Math::Mat4(1), data.nodes[0].rest.scale);
        const auto world = Math::Vec3(basis * Math::Vec4(rotated.value().translation, 0));
        EXPECT_NEAR(world.x, motion.value().translation.x, .0001);
        EXPECT_NEAR(world.z, motion.value().translation.z, .0001);
        EXPECT_NEAR(Math::Vec3(basis * Math::Vec4(rotated.value().up, 0)).z, 0, .0001);
    }

    TEST(RootMotionTest, NormalizedBlendUsesEachClipsOwnDurationAndMotionDistance) {
        auto data = motion_data();
        auto other = data.clips.front();
        other.name = "long";
        other.duration = 2;
        other.channels[0].times = {0, 2};
        other.channels[0].values = {{0, 0, 0, 0}, {8, 0, 0, 0}};
        data.clips.push_back(std::move(other));
        AnimationBlend blend{
            .clip = "move", .time = .75, .blends = {{"long", .5}}, .normalized_time = true};
        auto motion = animation_root_motion(data, blend, 2.25, {});
        ASSERT_TRUE(motion) << motion.error();
        EXPECT_NEAR(motion.value().translation.x, 7.5, .0001);
        EXPECT_NEAR(motion.value().translation.y, 0, .0001);
    }

    TEST(RootMotionTest, NativeTargetUsesFixedStepsAndKeepsFrozenInPlacePoseAcrossPauseAndStop) {
        auto data = std::make_shared<AnimationData>(motion_data());
        auto offset = data->clips[0];
        offset.name = "offset";
        offset.channels[0].values = {{100, 0, 0, 0}, {102, 1, 0, 0}};
        data->clips.push_back(std::move(offset));
        Scene scene;
        auto model = add_model(scene);
        const auto components = create_scene_component_registry();
        const auto serialized = SceneSerializer(components).serialize(scene);
        ASSERT_TRUE(serialized);
        auto cloned = SceneSerializer(components).deserialize(serialized.value());
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_EQ(
            cloned.value()->find_entity(model.get_uuid()).get_component<RootMotionComponent>(),
            model.get_component<RootMotionComponent>());
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = .1}));
        ASSERT_TRUE(runtime.add_system(root_system(data)));
        auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        ASSERT_TRUE(runtime.advance(.2));
        EXPECT_NEAR(model.get_component<TransformComponent>().translation.x, .4, .0001);
        const auto frozen = SceneExtractor::extract(scene);
        auto pose = sample_animation(*data, frozen.render_items[0].animation.get());
        ASSERT_TRUE(pose);
        EXPECT_NEAR(pose.value().matrices[0][3].x, 0, .0001);
        ASSERT_TRUE(runtime.advance(.03));
        EXPECT_NEAR(model.get_component<TransformComponent>().translation.x, .4, .0001);
        ASSERT_TRUE(runtime.advance(.07));
        EXPECT_NEAR(model.get_component<TransformComponent>().translation.x, .6, .0001);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(.2));
        EXPECT_NEAR(model.get_component<TransformComponent>().translation.x, .6, .0001);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(.2));
        EXPECT_NEAR(model.get_component<TransformComponent>().translation.x, .8, .0001);
        EXPECT_NEAR(frozen.render_items[0].animation->current.time, .2, .0001);
        model.edit_component<AnimatorComponent>([](auto& value) {
            value.clip = "offset";
            value.transition = .2f;
        });
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(.2));
        EXPECT_NEAR(model.get_component<TransformComponent>().translation.x, 1.2, .0001);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_animation_playback(model));
    }

    TEST(RootMotionTest, CharacterMotionUsesCollisionAndDoesNotAccumulateBlockedDistance) {
        auto data = std::make_shared<AnimationData>(motion_data());
        Scene scene;
        auto actor = scene.create_entity("actor");
        actor.add_component<CharacterControllerComponent>(
            CharacterControllerComponent{.gravity_scale = 0});
        auto model = add_model(scene, actor.get_uuid());
        ASSERT_TRUE(scene.set_parent(model, actor));
        auto wall = scene.create_entity("wall");
        wall.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        wall.add_component<ColliderComponent>();
        wall.edit_transform([](auto& value) {
            value.translation = {1.5f, 1, 0};
            value.scale = {.2f, 2, 10};
        });
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = .1}));
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        auto intent = std::make_unique<OppositeIntent>();
        intent->actor = actor;
        ASSERT_TRUE(runtime.add_system(std::move(intent)));
        ASSERT_TRUE(runtime.add_system(root_system(data)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        for(int i = 0; i < 20; ++i) {
            auto updated = runtime.advance(.1);
            ASSERT_TRUE(updated) << updated.error().message;
        }
        const auto stopped = actor.get_component<TransformComponent>().translation.x;
        EXPECT_GT(stopped, .8);
        EXPECT_LT(stopped, 1.2);
        scene.destroy_entity(wall);
        ASSERT_TRUE(runtime.advance(.1));
        ASSERT_TRUE(runtime.advance(.1));
        const auto resumed = actor.get_component<TransformComponent>().translation.x - stopped;
        EXPECT_GE(resumed, .18);
        EXPECT_LE(resumed, .42);
        actor.edit_component<CharacterControllerComponent>(
            [](auto& value) { value.enabled = false; });
        const auto disabled = actor.get_component<TransformComponent>().translation;
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_EQ(actor.get_component<TransformComponent>().translation, disabled);
        actor.edit_component<CharacterControllerComponent>(
            [](auto& value) { value.enabled = true; });
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_NEAR(actor.get_component<TransformComponent>().translation.x, disabled.x + .2, .02);
        model.edit_component<RootMotionComponent>([](auto& value) { value.vertical = true; });
        actor.edit_component<CharacterControllerComponent>(
            [](auto& value) { value.gravity_scale = 1; });
        const auto height = actor.get_component<TransformComponent>().translation.y;
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_NEAR(actor.get_component<TransformComponent>().translation.y, height + .1, .01);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(RootMotionTest, CharacterBlendUsesCurrentIntentBeforeTheFirstPhysicsMovement) {
        auto data = std::make_shared<AnimationData>(motion_data());
        data->clips.push_back({.name = "idle", .duration = 1});
        auto controller = std::make_shared<AnimationController>();
        controller->initial = "ground";
        controller->parameters = {
            {"speed", 0.f}, {"grounded", true}, {"rising", false}, {"crouched", false}};
        controller->states = {{"ground", {{0, "idle"}, {4, "move"}}, "speed"}};
        ASSERT_TRUE(controller->validate());
        Scene scene;
        auto actor = scene.create_entity("actor");
        actor.add_component<CharacterControllerComponent>(
            CharacterControllerComponent{.gravity_scale = 0});
        auto model = add_model(scene, actor.get_uuid());
        ASSERT_TRUE(scene.set_parent(model, actor));
        model.add_component<CharacterAnimationComponent>(
            CharacterAnimationComponent{actor.get_uuid()});
        model.edit_component<AnimatorComponent>(
            [](auto& value) { value.controller = AssetHandle(73); });
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = .1}));
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        auto intent = std::make_unique<OppositeIntent>();
        intent->actor = actor;
        ASSERT_TRUE(runtime.add_system(std::move(intent)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::CharacterAnimationSystem>()));
        ASSERT_TRUE(runtime.add_system(
            root_system(data, [controller](AssetHandle) { return controller; })));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.start(scene));
        auto updated = runtime.advance(.1);
        ASSERT_TRUE(updated) << updated.error().message;
        EXPECT_NEAR(actor.get_component<TransformComponent>().translation.x, .2, .0001);
        EXPECT_FLOAT_EQ(
            std::get<float>(model.get_component<AnimatorComponent>().parameters.at("speed")), 4);
    }

    TEST(RootMotionTest, RejectsDuplicateAndDynamicTransformOwners) {
        auto data = std::make_shared<AnimationData>(motion_data());
        Scene scene;
        auto actor = scene.create_entity("actor");
        auto first = add_model(scene, actor.get_uuid());
        auto second = add_model(scene, actor.get_uuid());
        ASSERT_TRUE(scene.set_parent(first, actor));
        ASSERT_TRUE(scene.set_parent(second, actor));
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(root_system(data)));
        EXPECT_FALSE(runtime.start(scene));
        scene.destroy_entity(second);
        actor.add_component<RigidBodyComponent>();
        EXPECT_FALSE(runtime.start(scene));
    }

    TEST(RootMotionTest, DemoReusesBuiltinCharacterWithCompatibleRootWalkLibrary) {
        const auto assets = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets";
        auto source = MeshImporter{}.import(assets / "animations/root_walk.glb");
        ASSERT_TRUE(source) << source.error();
        ASSERT_TRUE(source.value().animation);
        const auto default_path =
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY).parent_path()
            / "engine/resources/characters/default.glb";
        auto target = MeshImporter{}.import(default_path);
        ASSERT_TRUE(target) << target.error();
        ASSERT_TRUE(target.value().animation);
        auto library = AnimationLibrary::load(assets / "animations/root_motion.anilib");
        ASSERT_TRUE(library) << library.error();
        library.value().sources.emplace(library.value().entries[0].mesh, source.value().animation);
        auto clips = library.value().bind(*target.value().animation);
        ASSERT_TRUE(clips) << clips.error();
        const auto* walk = clips.value().find_clip("root_walk");
        ASSERT_TRUE(walk);
        auto motion = animation_root_motion(
            clips.value(), {.clip = "root_walk"}, walk->duration, {.node = "root"});
        ASSERT_TRUE(motion) << motion.error();
        EXPECT_NEAR(motion.value().translation.z, 2 * walk->duration, .001);
        AnimationPlayback frame{.current = {.clip = "root_walk", .time = .5},
            .root_motion = AnimationRootMotion{.node = "root"}};
        auto pose = sample_animation(clips.value(), &frame);
        ASSERT_TRUE(pose) << pose.error();
        const auto components = create_scene_component_registry();
        auto scene =
            SceneSerializer(components).load((assets / "scenes/root_motion.scene").string());
        ASSERT_TRUE(scene) << scene.error();
        auto loaded_controller =
            AnimationControllerSerializer{}.load(assets / "animations/root_motion.ani");
        ASSERT_TRUE(loaded_controller) << loaded_controller.error();
        auto controller =
            std::make_shared<AnimationController>(std::move(loaded_controller).value());
        auto shared_library = std::make_shared<AnimationLibrary>(library.value());
        auto animation = target.value().animation;
        Entity actor;
        scene.value()->each<const RootMotionComponent>(
            [&](Entity, const auto& root) { actor = scene.value()->find_entity(root.target); });
        ASSERT_TRUE(actor);
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([animation](AssetHandle) { return animation; },
                [controller](AssetHandle) { return controller; },
                [shared_library](AssetHandle) { return shared_library; },
                Gameplay::apply_root_motion)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        auto started = runtime.start(*scene.value());
        ASSERT_TRUE(started) << started.error().message;
        for(int i = 0; i < 180; ++i) {
            auto advanced = runtime.advance(1.0 / 60);
            ASSERT_TRUE(advanced) << advanced.error().message;
        }
        const auto position = actor.get_component<TransformComponent>().translation;
        EXPECT_GT(position.x, .8);
        EXPECT_LT(position.x, 1.2);
        EXPECT_NEAR(position.y, 0, .05);
        ASSERT_TRUE(runtime.stop());
    }
}
