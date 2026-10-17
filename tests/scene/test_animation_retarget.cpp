#include "animation/animation_retarget.h"
#include "asset/data/animation_library.h"
#include "asset/import/mesh_importer.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "animation/animation_system.h"
#include "inspector/animation_preview.h"
#include "gameplay/root_motion.h"
#include "gameplay/foot_placement.h"
#include "physics/physics_service.h"
#include "scene/scene_serializer.h"
#include "scene/scene_runtime.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/systems/physics_system.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        AnimationData rig(bool target) {
            AnimationData data;
            data.mesh_node = 0;
            data.nodes = {{target ? "mesh" : "source_mesh"}, {target ? "root" : "source_root"},
                {target ? "hip" : "source_hip", 1}, {target ? "foot" : "source_foot", 2}};
            data.nodes[1].rest.rotation = Math::angle_axis(
                target ? .7f : .2f, target ? Math::Vec3(0, 1, 0) : Math::Vec3(1, 0, 0));
            data.nodes[2].rest.translation = {0, target ? 1.f : .5f, 0};
            data.nodes[2].rest.rotation = Math::angle_axis(target ? -.3f : .4f, {0, 0, 1});
            data.nodes[3].rest.translation = {0, target ? 2.f : 1.f, 0};
            data.nodes[3].rest.rotation = Math::angle_axis(target ? .2f : -.5f, {1, 0, 0});
            data.evaluation_order = {0, 1, 2, 3};
            for(uint32_t i = 1; i < 4; ++i)
                data.joints.push_back({i, Math::Mat4(1), BoundingBox::from_point({0, 0, 0})});
            if(!target) {
                AnimationClip clip{.name = "motion", .duration = 1};
                for(uint32_t i = 1; i < 4; ++i) {
                    const auto rest = data.nodes[i].rest.rotation;
                    const auto end = rest * Math::angle_axis(.6f, {0, 0, 1});
                    clip.channels.push_back({i, AnimationChannel::Path::Rotation,
                        AnimationChannel::Interpolation::Linear, {0, 1},
                        {{rest.x, rest.y, rest.z, rest.w}, {end.x, end.y, end.z, end.w}}});
                }
                clip.channels.push_back({1, AnimationChannel::Path::Translation,
                    AnimationChannel::Interpolation::Linear, {0, 1}, {{0, 0, 0, 0}, {0, 0, 2, 0}}});
                data.clips.push_back(std::move(clip));
            }
            return data;
        }
        AnimationRetarget profile() {
            return {
                .nodes = {{"source_root", "root"}, {"source_hip", "hip"}, {"source_foot", "foot"}},
                .translations = {"source_root"}};
        }
        Math::Quat rotation(const Math::Mat4& matrix) {
            return glm::normalize(
                glm::quat_cast(Math::Mat3(matrix) / *Math::uniform_scale(matrix)));
        }
        void compare_rotations(const AnimationData& source, const AnimationData& target,
            const AnimationData& bound, double time) {
            AnimationPlayback frame{.current = {.clip = "motion", .time = time, .loop = false}};
            const auto first = sample_animation(source, &frame);
            const auto first_rest = sample_animation(source, nullptr);
            const auto second_rest = sample_animation(target, nullptr);
            const auto actual = sample_animation(bound, &frame);
            ASSERT_TRUE(first && first_rest && second_rest && actual);
            for(std::size_t i = 0; i < source.joints.size(); ++i) {
                const auto expected = rotation(first.value().bone_matrices[i])
                                      * glm::inverse(rotation(first_rest.value().bone_matrices[i]))
                                      * rotation(second_rest.value().bone_matrices[i]);
                EXPECT_NEAR(std::abs(glm::dot(rotation(actual.value().bone_matrices[i]), expected)),
                    1, .0001);
            }
        }
    }
    TEST(AnimationRetargetTest, SuggestionsUseUniqueJointSemanticsAndPreserveManualMappings) {
        auto source = rig(false), target = rig(true);
        source.nodes[1].name = "mixamorig:Hips";
        source.nodes[2].name = "mixamorig:LeftUpLeg";
        source.nodes[3].name = "mixamorig:LeftLeg";
        target.nodes[1].name = "pelvis";
        target.nodes[2].name = "thigh_l";
        target.nodes[3].name = "calf_l";
        auto matched = suggest_bone_mapping(source, target);
        ASSERT_TRUE(matched) << matched.error();
        EXPECT_EQ(matched.value().at("mixamorig:Hips"), "pelvis");
        EXPECT_EQ(matched.value().at("mixamorig:LeftUpLeg"), "thigh_l");
        EXPECT_EQ(matched.value().at("mixamorig:LeftLeg"), "calf_l");
        matched = suggest_bone_mapping(source, target, {{"mixamorig:LeftUpLeg", "calf_l"}});
        ASSERT_TRUE(matched);
        EXPECT_EQ(matched.value().at("mixamorig:LeftUpLeg"), "calf_l");
        EXPECT_FALSE(matched.value().contains("mixamorig:LeftLeg"));
        target.nodes[3].name = "left_thigh";
        matched = suggest_bone_mapping(source, target);
        ASSERT_TRUE(matched);
        EXPECT_FALSE(matched.value().contains("mixamorig:LeftUpLeg"));
        source.nodes[1].name = "骨盆";
        target.nodes[1].name = "脊柱";
        matched = suggest_bone_mapping(source, target);
        ASSERT_TRUE(matched);
        EXPECT_FALSE(matched.value().contains("骨盆"));
    }
    TEST(AnimationRetargetTest, ReferencePreviewUsesCalibrationWithoutChangingSourceOrMeshBasis) {
        const auto data = rig(true);
        auto frame = retarget_reference_pose(data, {{"hip", {0, 0, 45}}});
        ASSERT_TRUE(frame) << frame.error();
        EXPECT_EQ(frame.value().local_transforms[2].translation, data.nodes[2].rest.translation);
        const auto expected =
            data.nodes[2].rest.rotation * Math::Quat(glm::radians(Math::Vec3(0, 0, 45)));
        EXPECT_NEAR(
            std::abs(glm::dot(frame.value().local_transforms[2].rotation, expected)), 1, .00001);
        EXPECT_FALSE(retarget_reference_pose(data, {{"mesh", {0, 0, 0}}}));
        EXPECT_FALSE(retarget_reference_pose(data, {{"missing", {0, 0, 0}}}));
        EXPECT_FALSE(retarget_reference_pose(data, {{"hip", {0, 181, 0}}}));
        EXPECT_EQ(data.nodes[2].rest.rotation, rig(true).nodes[2].rest.rotation);
    }
    TEST(AnimationRetargetTest, ChainsResampleUnequalBoneCountsAndKeepTargetLengths) {
        AnimationData source{.mesh_node = 0};
        source.nodes = {{"mesh"}, {"upper"}, {"lower", 1, {.translation = {0, 1, 0}}},
            {"tip", 2, {.translation = {0, 1, 0}}}};
        source.evaluation_order = {0, 1, 2, 3};
        for(uint32_t i = 1; i < source.nodes.size(); ++i)
            source.joints.push_back({i, Math::Mat4(1), BoundingBox::from_point({0, 0, 0})});
        const auto end = Math::angle_axis(.8f, {0, 0, 1});
        source.clips = {{"bend", 1,
            {{2, AnimationChannel::Path::Rotation, AnimationChannel::Interpolation::Linear, {0, 1},
                {{0, 0, 0, 1}, {end.x, end.y, end.z, end.w}}}}}};
        auto target = source;
        target.clips.clear();
        target.nodes[2].name = "twist";
        target.nodes[3].name = "lower";
        target.nodes.push_back({"tip", 3, {.translation = {0, 1, 0}}});
        target.evaluation_order.push_back(4);
        target.joints.push_back({4, Math::Mat4(1), BoundingBox::from_point({0, 0, 0})});
        AnimationRetarget settings;
        settings.chains = {{"upper", "tip", "upper", "tip"}};
        auto result = retarget_animation(source, target, settings);
        ASSERT_TRUE(result) << result.error();
        for(auto time : {0., .5, 1.}) {
            AnimationPlayback frame{.current = {.clip = "bend", .time = time, .loop = false}};
            auto pose = sample_animation(result.value(), &frame);
            ASSERT_TRUE(pose) << pose.error();
            for(size_t i = 0; i < target.nodes.size(); ++i)
                EXPECT_EQ(
                    pose.value().local_transforms[i].translation, target.nodes[i].rest.translation);
            const auto expected = Math::angle_axis(float(time) * .8f * (2.f / 3), {0, 0, 1});
            EXPECT_NEAR(
                std::abs(glm::dot(pose.value().local_transforms[2].rotation, expected)), 1, .0001);
            EXPECT_NEAR(std::abs(glm::dot(rotation(pose.value().bone_matrices.back()),
                            Math::angle_axis(float(time) * .8f, {0, 0, 1}))),
                1, .0001);
        }
        AnimationLibrary library;
        library.entries = {{"bent", AssetHandle(12), "bend"}};
        library.retargets = {{AssetHandle(12), settings}};
        auto text = library.serialize();
        ASSERT_TRUE(text);
        auto restored = AnimationLibrary::deserialize(text.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value(), library);
        settings.chains.push_back(settings.chains.front());
        EXPECT_FALSE(retarget_animation(source, target, settings));
        settings.chains = {{"tip", "upper", "upper", "tip"}};
        EXPECT_FALSE(retarget_animation(source, target, settings));
        settings.chains = {{"upper", "tip", "upper", "missing"}};
        EXPECT_FALSE(retarget_animation(source, target, settings));
        settings.chains = {{"upper", "tip", "upper", "tip", 0}};
        auto rest = retarget_animation(source, target, settings);
        ASSERT_TRUE(rest);
        AnimationPlayback frame{.current = {.clip = "bend", .time = .5, .loop = false}};
        EXPECT_NEAR(sample_animation(rest.value(), &frame).value().local_transforms[2].rotation.w,
            1, .0001);
        settings.chains.front().weight = NAN;
        EXPECT_FALSE(settings.validate());
    }

    TEST(
        AnimationRetargetTest, DifferentNamesRestAxesAndLengthsKeepTargetSkeletonAndWorldRotation) {
        const auto source = rig(false), target = rig(true);
        auto bound = retarget_animation(source, target, profile());
        ASSERT_TRUE(bound) << bound.error();
        EXPECT_EQ(bound.value().nodes[3].rest.translation, target.nodes[3].rest.translation);
        EXPECT_EQ(bound.value().nodes[2].rest.rotation, target.nodes[2].rest.rotation);
        for(const double t : {0., .1, .5, 1.})
            compare_rotations(source, target, bound.value(), t);
        AnimationPlayback frame{.current = {.clip = "motion", .time = .5, .loop = false}};
        const auto pose = sample_animation(bound.value(), &frame);
        ASSERT_TRUE(pose);
        EXPECT_NEAR(pose.value().bone_matrices[0][3].z, 2, .0001);
        EXPECT_EQ(source.nodes[3].rest.translation, Math::Vec3(0, 1, 0));
        auto manual = profile();
        manual.translation_scale = .5f;
        auto small = retarget_animation(source, target, manual);
        ASSERT_TRUE(small);
        EXPECT_NEAR(
            sample_animation(small.value(), &frame).value().bone_matrices[0][3].z, .5, .0001);
    }
    TEST(AnimationRetargetTest, CubicTangentsAndStepKeysPreserveCurveAndDuration) {
        auto source = rig(false);
        auto& channel = source.clips[0].channels[1];
        const auto first = channel.values.front(), last = channel.values.back();
        channel.interpolation = AnimationChannel::Interpolation::CubicSpline;
        channel.values = {
            {0, 0, 0, 0}, first, {.1f, .2f, -.1f, .05f}, {-.1f, .05f, .2f, 0}, last, {0, 0, 0, 0}};
        const auto target = rig(true);
        auto bound = retarget_animation(source, target, profile());
        ASSERT_TRUE(bound) << bound.error();
        EXPECT_EQ(bound.value().clips[0].duration, source.clips[0].duration);
        for(const double t : {.1, .37, .8, 1.})
            compare_rotations(source, target, bound.value(), t);
        channel.interpolation = AnimationChannel::Interpolation::Step;
        channel.values = {first, last};
        bound = retarget_animation(source, target, profile());
        ASSERT_TRUE(bound);
        compare_rotations(source, target, bound.value(), .7);
    }
    TEST(AnimationRetargetTest, StaticHelperBetweenMappedBonesDoesNotRequireIdenticalHierarchy) {
        const auto source = rig(false);
        auto target = rig(true);
        target.nodes.push_back({"helper", 2,
            {.translation = {0, .5f, 0}, .rotation = Math::angle_axis(.3f, {0, 1, 0})}});
        target.nodes[3].parent = 4;
        target.nodes[3].rest.translation.y = 1.5f;
        target.evaluation_order = {0, 1, 2, 4, 3};
        auto bound = retarget_animation(source, target, profile());
        ASSERT_TRUE(bound) << bound.error();
        compare_rotations(source, target, bound.value(), .6);
        EXPECT_EQ(bound.value().nodes[3].parent, 4u);
    }
    TEST(AnimationRetargetTest, BadMappingsHierarchyAnimatedAncestorsAndScaleAreRejected) {
        auto source = rig(false), target = rig(true);
        auto settings = profile();
        settings.nodes["source_foot"] = "missing";
        EXPECT_FALSE(retarget_animation(source, target, settings));
        settings = profile();
        settings.nodes["source_foot"] = "hip";
        EXPECT_FALSE(retarget_animation(source, target, settings));
        EXPECT_FALSE(retarget_animation(source, target, {}));
        settings = profile();
        target.nodes[3].parent = 1;
        EXPECT_FALSE(retarget_animation(source, target, settings));
        target = rig(true);
        target.nodes[2].rest.scale.x = 2;
        EXPECT_FALSE(retarget_animation(source, target, settings));
        target = rig(true);
        source.nodes.push_back({"unmapped_helper", 1});
        source.nodes[2].parent = 4;
        source.evaluation_order = {0, 1, 4, 2, 3};
        source.clips[0].channels.push_back({4, AnimationChannel::Path::Translation,
            AnimationChannel::Interpolation::Linear, {0, 1}, {{0, 0, 0, 0}, {1, 0, 0, 0}}});
        EXPECT_FALSE(retarget_animation(source, target, settings));
        source = rig(false);
        source.clips[0].channels.push_back({2, AnimationChannel::Path::Scale,
            AnimationChannel::Interpolation::Linear, {0, 1}, {{1, 1, 1, 0}, {2, 2, 2, 0}}});
        EXPECT_FALSE(retarget_animation(source, target, settings));
        source = rig(false);
        source.clips[0].channels.push_back({0, AnimationChannel::Path::Translation,
            AnimationChannel::Interpolation::Linear, {0, 1}, {{0, 0, 0, 0}, {1, 0, 0, 0}}});
        EXPECT_FALSE(retarget_animation(source, target, settings));
        settings.translation_scale = NAN;
        EXPECT_FALSE(settings.validate());
    }
    TEST(AnimationRetargetTest, LibraryOptInRoundTripsAndCompatibleBindingStaysStrict) {
        const auto source = std::make_shared<AnimationData>(rig(false));
        const auto target = rig(true);
        AnimationLibrary library;
        library.entries = {{"retargeted", AssetHandle(12), "motion"}};
        library.sources = {{AssetHandle(12), source}};
        EXPECT_FALSE(library.bind(target));
        library.retargets = {{AssetHandle(12), profile()}};
        auto bound = library.bind(target);
        ASSERT_TRUE(bound) << bound.error();
        EXPECT_EQ(bound.value().clips[0].name, "retargeted");
        auto text = library.serialize();
        ASSERT_TRUE(text) << text.error();
        auto restored = AnimationLibrary::deserialize(text.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value(), library);
        EXPECT_TRUE(restored.value().sources.empty());
        restored.value().sources = library.sources;
        EXPECT_TRUE(restored.value().bind(target));
        auto unknown = text.value();
        unknown.insert(unknown.find("\"nodes\""), "\"unknown\":1,");
        EXPECT_FALSE(AnimationLibrary::deserialize(unknown));
    }
    TEST(AnimationRetargetTest, AuthoredGlbRetargetsNamesAxesAndProportionsIntoBuiltinCharacter) {
        const auto assets = std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets";
        auto source = MeshImporter{}.import(assets / "animations/retarget_walk.glb");
        auto original = MeshImporter{}.import(assets / "animations/root_walk.glb");
        auto target = MeshImporter{}.import(
            assets.parent_path().parent_path() / "engine/resources/characters/default.glb");
        ASSERT_TRUE(source && original && target);
        ASSERT_TRUE(
            source.value().animation && original.value().animation && target.value().animation);
        auto definition = AnimationLibrary::load(assets / "animations/retarget.anilib");
        auto reference = AnimationLibrary::load(assets / "animations/root_motion.anilib");
        ASSERT_TRUE(definition && reference);
        definition.value().sources = {
            {definition.value().entries[0].mesh, source.value().animation}};
        reference.value().sources = {
            {reference.value().entries[0].mesh, original.value().animation}};
        const auto bound = definition.value().bind(*target.value().animation);
        const auto expected = reference.value().bind(*target.value().animation);
        ASSERT_TRUE(bound) << bound.error();
        ASSERT_TRUE(expected) << expected.error();
        auto chain_profile = AnimationLibrary::load(assets / "animations/chain_retarget.anilib");
        ASSERT_TRUE(chain_profile) << chain_profile.error();
        chain_profile.value().sources = definition.value().sources;
        const auto chained = chain_profile.value().bind(*target.value().animation);
        ASSERT_TRUE(chained) << chained.error();
        for(double time : {0., .1, .37, .6}) {
            AnimationPlayback frame{.current = {.clip = "retarget_walk", .time = time},
                .root_motion = AnimationRootMotion{.node = "root"}};
            const auto chain_pose = sample_animation(chained.value(), &frame);
            const auto node_pose = sample_animation(bound.value(), &frame);
            ASSERT_TRUE(chain_pose && node_pose);
            for(size_t joint = 0; joint < chain_pose.value().matrices.size(); ++joint)
                for(int column = 0; column < 4; ++column)
                    EXPECT_LT(glm::length(chain_pose.value().matrices[joint][column]
                                          - node_pose.value().matrices[joint][column]),
                        .005f);
        }
        auto suggested = suggest_bone_mapping(*source.value().animation, *target.value().animation);
        ASSERT_TRUE(suggested) << suggested.error();
        auto automatic = definition.value();
        automatic.retargets.begin()->second.nodes = suggested.value();
        ASSERT_TRUE(automatic.bind(*target.value().animation));
        auto compatible_only = definition.value();
        compatible_only.retargets.clear();
        EXPECT_FALSE(compatible_only.bind(*target.value().animation));
        for(const double time : {0., .1, .37, .6}) {
            AnimationPlayback frame{.current = {.clip = "retarget_walk", .time = time},
                .root_motion = AnimationRootMotion{.node = "root"}};
            auto actual_pose = sample_animation(bound.value(), &frame);
            frame.current.clip = "root_walk";
            auto expected_pose = sample_animation(expected.value(), &frame);
            ASSERT_TRUE(actual_pose && expected_pose);
            ASSERT_EQ(actual_pose.value().matrices.size(), expected_pose.value().matrices.size());
            for(std::size_t i = 0; i < actual_pose.value().matrices.size(); ++i)
                for(int column = 0; column < 4; ++column)
                    EXPECT_LT(glm::length(actual_pose.value().matrices[i][column]
                                          - expected_pose.value().matrices[i][column]),
                        .002);
        }
        auto actual_motion = animation_root_motion(
            bound.value(), {.clip = "retarget_walk", .time = .75}, 2.25, {.node = "root"});
        auto expected_motion = animation_root_motion(
            expected.value(), {.clip = "root_walk", .time = .75}, 2.25, {.node = "root"});
        ASSERT_TRUE(actual_motion && expected_motion);
        EXPECT_LT(
            glm::length(actual_motion.value().translation - expected_motion.value().translation),
            .001);
        const auto original_controller =
            AnimationControllerSerializer{}.load(assets / "animations/root_motion.ani");
        const auto retarget_controller =
            AnimationControllerSerializer{}.load(assets / "animations/retarget.ani");
        ASSERT_TRUE(original_controller && retarget_controller);
        const auto original_metadata =
            MetadataSerializer{}.load(assets / "animations/root_motion.ani.meta");
        ASSERT_TRUE(original_metadata);
        auto first = std::make_shared<AnimationController>(original_controller.value());
        auto second = std::make_shared<AnimationController>(retarget_controller.value());
        auto first_library = std::make_shared<AnimationLibrary>(reference.value());
        auto second_library = std::make_shared<AnimationLibrary>(definition.value());
        auto scene = SceneSerializer(create_scene_component_registry())
                         .load((assets / "scenes/retarget.scene").string());
        ASSERT_TRUE(scene) << scene.error();
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<AnimationSystem>(
            [data = target.value().animation](AssetHandle) { return data; },
            [first, second, original_handle = original_metadata.value().handle](
                AssetHandle handle) { return handle == original_handle ? first : second; },
            [first, first_library, second_library](AssetHandle handle) {
                return handle == first->library ? first_library : second_library;
            },
            Gameplay::apply_root_motion)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<Gameplay::FootPlacementSystem>()));
        ASSERT_TRUE(runtime.start(*scene.value()));
        for(int i = 0; i < 180; ++i) {
            auto step = runtime.advance(1.0 / 60);
            ASSERT_TRUE(step) << step.error().message;
        }
        std::vector<Math::Vec3> positions;
        scene.value()->each<const RootMotionComponent>([&](Entity model, const auto& root) {
            const auto frame = scene.value()->get_animation_playback(model);
            ASSERT_TRUE(frame && frame->pose);
            EXPECT_TRUE(frame->pose_error.empty()) << frame->pose_error;
            const Entity actor = scene.value()->find_entity(root.target);
            positions.push_back(actor.get_component<TransformComponent>().translation);
        });
        ASSERT_EQ(positions.size(), 2u);
        EXPECT_NEAR(positions[0].x, positions[1].x, .01);
        EXPECT_NEAR(positions[0].y, positions[1].y, .01);
        EXPECT_GT(positions[0].x, .8);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AnimationRetargetTest, RuntimeAndPreviewRebindNewProfilesWithoutChangingFrozenSnapshots) {
        const auto target = std::make_shared<AnimationData>(rig(true));
        const auto source = std::make_shared<AnimationData>(rig(false));
        auto library = std::make_shared<AnimationLibrary>();
        library->entries = {{"retargeted", AssetHandle(12), "motion"}};
        library->sources = {{AssetHandle(12), source}};
        library->retargets = {{AssetHandle(12), profile()}};
        auto controller = std::make_shared<AnimationController>();
        controller->library = AssetHandle(7);
        controller->states[0].motions = {{0, "retargeted"}};
        Scene scene;
        auto model = scene.create_entity();
        model.add_component<MeshRendererComponent>();
        model.add_component<AnimatorComponent>(AnimatorComponent{.controller = AssetHandle(8)});
        model.add_component<RootMotionComponent>(
            RootMotionComponent{.node = "root", .rotation = false});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AnimationSystem>([target](AssetHandle) { return target; },
                [controller](AssetHandle) { return controller; },
                [&library](AssetHandle) { return library; }, Gameplay::apply_root_motion)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(.1));
        const auto frozen = scene.get_animation_playback(model);
        const auto before = model.get_component<TransformComponent>().translation;
        EXPECT_NEAR(before.z, .4, .0001);
        CometEditor::AnimationPreview preview;
        preview.bind({1, model.get_uuid()}, target, "");
        ASSERT_TRUE(preview.set_library(library));
        ASSERT_TRUE(preview.select_sample({.clip = "retargeted", .time = .5}, true));
        const auto preview_frozen = preview.snapshot();
        auto changed = std::make_shared<AnimationLibrary>(*library);
        changed->retargets[AssetHandle(12)].translation_scale = .5f;
        library = changed;
        ASSERT_TRUE(runtime.advance(.1));
        EXPECT_NEAR(model.get_component<TransformComponent>().translation.z - before.z, .1, .0001);
        const auto current = scene.get_animation_playback(model);
        EXPECT_NE(current->clips, frozen->clips);
        auto old_motion = animation_root_motion(
            *frozen->clips, {.clip = "retargeted", .time = .1}, .2, {.node = "root"});
        auto new_motion = animation_root_motion(
            *current->clips, {.clip = "retargeted", .time = .1}, .2, {.node = "root"});
        ASSERT_TRUE(old_motion && new_motion);
        EXPECT_NEAR(old_motion.value().translation.z, .4, .0001);
        EXPECT_NEAR(new_motion.value().translation.z, .1, .0001);
        ASSERT_TRUE(preview.set_library(library));
        EXPECT_EQ(preview.snapshot()->current.time, preview_frozen->current.time);
        auto old_pose = sample_animation(*target, preview_frozen.get());
        auto new_pose = sample_animation(*target, preview.snapshot().get());
        ASSERT_TRUE(old_pose && new_pose);
        EXPECT_NEAR(old_pose.value().bone_matrices[0][3].z, 2, .0001);
        EXPECT_NEAR(new_pose.value().bone_matrices[0][3].z, .5, .0001);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AnimationRetargetTest, ReferencePoseCalibrationIsBoundWithoutMutatingSharedSources) {
        auto source = rig(false), target = rig(true);
        auto settings = profile();
        settings.target_pose["hip"] = {0, 0, 20};
        settings.source_pose["source_root"] = {5, 0, 0};
        auto calibrated = retarget_animation(source, target, settings);
        ASSERT_TRUE(calibrated) << calibrated.error();
        auto reference_source = source, reference_target = target;
        reference_source.nodes[1].rest.rotation =
            glm::normalize(reference_source.nodes[1].rest.rotation
                           * Math::Quat(glm::radians(Math::Vec3(5, 0, 0))));
        reference_target.nodes[2].rest.rotation =
            glm::normalize(reference_target.nodes[2].rest.rotation
                           * Math::Quat(glm::radians(Math::Vec3(0, 0, 20))));
        AnimationPlayback frame{.current = {.clip = "motion", .time = .37, .loop = false}};
        auto input = sample_animation(source, &frame),
             source_rest = sample_animation(reference_source, nullptr),
             target_rest = sample_animation(reference_target, nullptr),
             output = sample_animation(calibrated.value(), &frame);
        ASSERT_TRUE(input && source_rest && target_rest && output);
        for(size_t i = 0; i < source.joints.size(); ++i) {
            const auto expected = rotation(input.value().bone_matrices[i])
                                  * glm::inverse(rotation(source_rest.value().bone_matrices[i]))
                                  * rotation(target_rest.value().bone_matrices[i]);
            EXPECT_NEAR(
                std::abs(glm::dot(rotation(output.value().bone_matrices[i]), expected)), 1, .0001);
        }
        EXPECT_EQ(calibrated.value().nodes[2].rest.rotation, target.nodes[2].rest.rotation);
        AnimationLibrary library;
        library.entries = {{"walk", AssetHandle{12}, "motion"}};
        library.retargets = {{AssetHandle{12}, settings}};
        auto text = library.serialize();
        ASSERT_TRUE(text);
        auto decoded = AnimationLibrary::deserialize(text.value());
        ASSERT_TRUE(decoded) << decoded.error();
        EXPECT_EQ(decoded.value(), library);
        settings.target_pose["missing"] = {0, 0, 0};
        EXPECT_FALSE(retarget_animation(source, target, settings));
        settings.target_pose.clear();
        settings.source_pose["source_mesh"] = {1, 0, 0};
        EXPECT_FALSE(retarget_animation(source, target, settings));
    }

}
