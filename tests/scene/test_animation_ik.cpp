#include "animation/animation_data.h"
#include "scene/scene.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    namespace {
        AnimationData limb() {
            AnimationData data;
            data.mesh_node = 0;
            data.nodes = {{"mesh"}, {"hip"}, {"knee", 1, {.translation = {0, -1, 0}}},
                {"foot", 2, {.translation = {0, -1, 0}}}, {"toe", 3, {.translation = {0, 0, .3f}}}};
            data.evaluation_order = {0, 1, 2, 3, 4};
            for(uint32_t i = 1; i < 5; ++i)
                data.joints.push_back({i, Math::Mat4(1), BoundingBox::from_point({0, 0, 0})});
            return data;
        }
    }
    TEST(AnimationIKTest, ReachesTargetPreservesBoneLengthsAndMatchesFootRotation) {
        const auto data = limb();
        auto pose = sample_animation(data, nullptr);
        ASSERT_TRUE(pose);
        const AnimationIKConstraint constraint{.tip = 3,
            .target = {.3f, -1.5f, .2f},
            .pole = {0, 0, 2},
            .rotation = Math::angle_axis(.2f, {0, 0, 1})};
        auto result = apply_animation_ik(data, pose.value(), std::span(&constraint, 1));
        ASSERT_TRUE(result) << result.error();
        const auto& bones = result.value().bone_matrices;
        EXPECT_LT(Math::length(Math::Vec3(bones[2][3]) - constraint.target), .0001);
        EXPECT_NEAR(Math::length(Math::Vec3(bones[1][3] - bones[0][3])), 1, .0001);
        EXPECT_NEAR(Math::length(Math::Vec3(bones[2][3] - bones[1][3])), 1, .0001);
        EXPECT_GT(bones[1][3].z, .1);
        EXPECT_NEAR(std::abs(glm::dot(glm::quat_cast(Math::Mat3(bones[2])), *constraint.rotation)),
            1, .0001);
        EXPECT_LT(Math::length(Math::Vec3(bones[3][3]) - constraint.target
                               - *constraint.rotation * Math::Vec3(0, 0, .3f)),
            .0001);
        EXPECT_EQ(data.nodes[2].rest.translation, Math::Vec3(0, -1, 0));
        EXPECT_EQ(pose.value().bone_matrices[2][3], Math::Vec4(0, -2, 0, 1));
        EXPECT_TRUE(result.value().bounds.is_valid());
    }
    TEST(AnimationIKTest, ClampsUnreachableTargetsAndHandlesCollinearAndZeroDistanceHints) {
        const auto data = limb();
        const auto pose = sample_animation(data, nullptr);
        ASSERT_TRUE(pose);
        for(const auto target : {Math::Vec3(0, -10, 0), Math::Vec3(0), Math::Vec3(0, 1, 0)}) {
            SCOPED_TRACE(target.y);
            const AnimationIKConstraint constraint{.tip = 3, .target = target, .pole = target};
            auto solved = apply_animation_ik(data, pose.value(), std::span(&constraint, 1));
            ASSERT_TRUE(solved) << solved.error();
            const auto foot = Math::Vec3(solved.value().bone_matrices[2][3]);
            EXPECT_TRUE(Math::is_finite(foot));
            EXPECT_LE(Math::length(foot), 2.0001);
            if(Math::length(target) > 0)
                EXPECT_NEAR(glm::dot(glm::normalize(foot), glm::normalize(target)), 1, .0001);
        }
    }
    TEST(AnimationIKTest, PartialWeightsComposeInLocalRotationsAndInvalidInputsAreDiagnosed) {
        auto data = limb();
        const auto pose = sample_animation(data, nullptr);
        ASSERT_TRUE(pose);
        AnimationIKConstraint constraint{.tip = 3, .target = {0, -1.5f, .2f}, .pole = {0, 0, 1}};
        auto full = apply_animation_ik(data, pose.value(), std::span(&constraint, 1));
        ASSERT_TRUE(full);
        constraint.weight = .5f;
        auto half = apply_animation_ik(data, pose.value(), std::span(&constraint, 1));
        ASSERT_TRUE(half);
        EXPECT_NEAR(std::abs(glm::dot(half.value().local_transforms[1].rotation,
                        glm::slerp(data.nodes[1].rest.rotation,
                            full.value().local_transforms[1].rotation, .5f))),
            1, .0001);
        constraint.weight = 0;
        EXPECT_EQ(
            apply_animation_ik(data, pose.value(), std::span(&constraint, 1)).value().matrices,
            pose.value().matrices);
        constraint.weight = 1;
        constraint.tip = 1;
        EXPECT_FALSE(apply_animation_ik(data, pose.value(), std::span(&constraint, 1)));
        constraint.tip = 3;
        constraint.target.x = NAN;
        EXPECT_FALSE(apply_animation_ik(data, pose.value(), std::span(&constraint, 1)));
        constraint.target.x = 0;
        constraint.rotation = Math::Quat(0, 0, 0, 0);
        EXPECT_FALSE(apply_animation_ik(data, pose.value(), std::span(&constraint, 1)));
        constraint.rotation.reset();
        auto foreign = std::make_shared<AnimationData>(data);
        AnimationPlayback frame{.ik = {constraint}, .pose_source = foreign};
        const auto stale = sample_animation(data, &frame);
        ASSERT_FALSE(stale);
        EXPECT_NE(stale.error().find("skeleton changed"), std::string::npos);
        data.nodes[1].rest.scale.x = 2;
        const auto scaled = sample_animation(data, nullptr);
        ASSERT_TRUE(scaled);
        EXPECT_FALSE(apply_animation_ik(data, scaled.value(), std::span(&constraint, 1)));
    }

    TEST(AnimationIKTest, SceneReusesOnlyUnconstrainedPoseWithIdenticalSamplingInputs) {
        auto data = std::make_shared<AnimationData>(limb());
        data->clips = {{"idle", 1}};
        Scene scene;
        auto model = scene.create_entity("model");
        model.add_component<FootPlacementComponent>();
        scene.set_animation_source(model, data);
        scene.set_animation_playback(
            model, std::make_shared<const AnimationPlayback>(
                       AnimationPlayback{.current = {.clip = "idle", .time = .25, .loop = false}}));
        const auto base = scene.get_animation_playback(model);
        ASSERT_TRUE(base && base->pose);
        const auto before = scene.get_animation_statistics();
        auto constrained = std::make_shared<AnimationPlayback>(*base);
        constrained->ik = {{.tip = 3, .target = {.3f, -1.5f, .2f}, .pole = {0, 0, 2}}};
        const auto expected = sample_animation(*data, constrained.get());
        ASSERT_TRUE(expected);
        scene.set_animation_playback(model, constrained);
        auto actual = scene.get_animation_playback(model);
        ASSERT_TRUE(actual && actual->pose);
        EXPECT_EQ(actual->pose->matrices, expected.value().matrices);
        EXPECT_EQ(actual->pose->bone_matrices, expected.value().bone_matrices);
        EXPECT_EQ(actual->pose->bounds.minimum, expected.value().bounds.minimum);
        EXPECT_EQ(actual->pose->bounds.maximum, expected.value().bounds.maximum);
        EXPECT_EQ(scene.get_animation_statistics().pose_resolutions, before.pose_resolutions);
        EXPECT_EQ(scene.get_animation_statistics().ik_pose_reuses, before.ik_pose_reuses + 1);
        EXPECT_EQ(base->pose->bone_matrices[2][3], Math::Vec4(0, -2, 0, 1));

        // 已约束的姿态不能再次作为基础，否则部分权重会重复叠加。
        constrained = std::make_shared<AnimationPlayback>(*actual);
        constrained->ik[0].weight = .5f;
        const auto partial = sample_animation(*data, constrained.get());
        ASSERT_TRUE(partial);
        scene.set_animation_playback(model, constrained);
        EXPECT_EQ(scene.get_animation_playback(model)->pose->matrices, partial.value().matrices);
        EXPECT_EQ(scene.get_animation_statistics().pose_resolutions, before.pose_resolutions + 1);
        EXPECT_EQ(scene.get_animation_statistics().ik_pose_reuses, before.ik_pose_reuses + 1);

        scene.set_animation_playback(model, base);
        const auto cleared = scene.get_animation_statistics();
        constrained = std::make_shared<AnimationPlayback>(*base);
        constrained->current.time = .5;
        constrained->ik = {{.tip = 3, .target = {.3f, -1.5f, .2f}, .pole = {0, 0, 2}}};
        scene.set_animation_playback(model, constrained);
        EXPECT_EQ(scene.get_animation_statistics().pose_resolutions, cleared.pose_resolutions + 1);
        EXPECT_EQ(scene.get_animation_statistics().ik_pose_reuses, cleared.ik_pose_reuses);

        scene.set_animation_playback(model, base);
        auto replacement = std::make_shared<AnimationData>(*data);
        replacement->nodes[2].rest.translation.y = -1.2f;
        scene.set_animation_source(model, replacement);
        constrained = std::make_shared<AnimationPlayback>(*scene.get_animation_playback(model));
        constrained->pose_source = data; // 旧骨架输入不能命中局部姿态复用。
        constrained->ik = {{.tip = 3, .target = {.3f, -1.5f, .2f}, .pole = {0, 0, 2}}};
        const auto replaced = scene.get_animation_statistics();
        scene.set_animation_playback(model, constrained);
        EXPECT_EQ(scene.get_animation_playback(model)->pose_source, replacement);
        EXPECT_EQ(scene.get_animation_statistics().pose_resolutions, replaced.pose_resolutions + 1);
        EXPECT_EQ(scene.get_animation_statistics().ik_pose_reuses, replaced.ik_pose_reuses);
    }

    TEST(AnimationIKTest, SceneReuseKeepsInvalidConstraintAndMissingClipFallbacks) {
        auto data = std::make_shared<AnimationData>(limb());
        data->clips = {{"idle", 1}};
        Scene scene;
        auto model = scene.create_entity("model");
        model.add_component<FootPlacementComponent>();
        scene.set_animation_source(model, data);
        scene.set_animation_playback(model, std::make_shared<const AnimationPlayback>(
                                                AnimationPlayback{.current = {.clip = "idle"}}));
        auto input = std::make_shared<AnimationPlayback>(*scene.get_animation_playback(model));
        input->ik = {{.tip = NO_ANIMATION_NODE}};
        scene.set_animation_playback(model, input);
        auto failed = scene.get_animation_playback(model);
        ASSERT_TRUE(failed && failed->pose);
        EXPECT_NE(failed->pose_error.find("IK tip"), std::string::npos);
        EXPECT_EQ(scene.get_animation_statistics().ik_pose_reuses, 0u);
        const auto bind = sample_animation(*data, nullptr);
        ASSERT_TRUE(bind);
        EXPECT_EQ(failed->pose->matrices, bind.value().matrices);

        input = std::make_shared<AnimationPlayback>(*failed);
        input->current.clip = "missing";
        input->ik.clear();
        scene.set_animation_playback(model, input);
        input = std::make_shared<AnimationPlayback>(*scene.get_animation_playback(model));
        input->ik = {{.tip = 3, .target = {.3f, -1.5f, .2f}, .pole = {0, 0, 2}}};
        const auto before = scene.get_animation_statistics();
        scene.set_animation_playback(model, input);
        EXPECT_NE(
            scene.get_animation_playback(model)->pose_error.find("missing"), std::string::npos);
        EXPECT_EQ(scene.get_animation_statistics().pose_resolutions, before.pose_resolutions + 1);
        EXPECT_EQ(scene.get_animation_statistics().ik_pose_reuses, 0u);
    }
}
