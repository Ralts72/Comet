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
    TEST(AnimationIKTest, FullBodyCouplesReachAndSupportWithoutStretchingOrChangingContent) {
        AnimationData data;
        data.mesh_node = 0;
        data.nodes = {{"mesh"}, {"pelvis", 0, {.translation = {0, 1.5f, 0}}},
            {"chest", 1, {.translation = {0, .7f, 0}}},
            {"hip_l", 1, {.translation = {-.25f, 0, 0}}},
            {"knee_l", 3, {.translation = {0, -.75f, 0}}},
            {"foot_l", 4, {.translation = {0, -.75f, 0}}},
            {"hip_r", 1, {.translation = {.25f, 0, 0}}},
            {"knee_r", 6, {.translation = {0, -.75f, 0}}},
            {"foot_r", 7, {.translation = {0, -.75f, 0}}},
            {"arm_l", 2, {.translation = {-.25f, 0, 0}}},
            {"elbow_l", 9, {.translation = {-.65f, 0, 0}}},
            {"hand_l", 10, {.translation = {-.65f, 0, 0}}},
            {"arm_r", 2, {.translation = {.25f, 0, 0}}},
            {"elbow_r", 12, {.translation = {.65f, 0, 0}}},
            {"hand_r", 13, {.translation = {.65f, 0, 0}}}};
        for(uint32_t i = 0; i < data.nodes.size(); ++i) {
            data.evaluation_order.push_back(i);
            if(i)
                data.joints.push_back({i, Math::Mat4(1), BoundingBox::from_point({0, 0, 0})});
        }
        ASSERT_TRUE(validate_animation(data));
        const auto base = sample_animation(data, nullptr);
        ASSERT_TRUE(base);
        const std::array constraints{
            AnimationIKConstraint{.tip = 11, .target = {-1.15f, 2.2f, 1.2f}, .pole = {-1, 2, 0}},
            AnimationIKConstraint{.tip = 14, .target = {1.15f, 2.2f, 1.2f}, .pole = {1, 2, 0}},
            AnimationIKConstraint{.tip = 5, .target = {-.25f, 0, 0}, .pole = {-.25f, 1, 1}},
            AnimationIKConstraint{.tip = 8, .target = {.25f, 0, 0}, .pole = {.25f, 1, 1}}};
        auto independent = apply_animation_constraints(data, base.value(), constraints);
        ASSERT_TRUE(independent);
        AnimationBodyIK body{.root = 1, .iterations = 12};
        auto coupled =
            apply_animation_constraints(data, base.value(), constraints, nullptr, {}, &body);
        ASSERT_TRUE(coupled) << coupled.error();
        const auto error = [&](const AnimationPose& pose) {
            float sum = 0;
            for(const auto& goal : constraints)
                sum += Math::length(Math::Vec3(pose.bone_matrices[goal.tip - 1][3]) - goal.target);
            return sum;
        };
        EXPECT_LT(error(coupled.value()), error(independent.value()));
        EXPECT_GT(Math::length(coupled.value().local_transforms[1].translation
                               - data.nodes[1].rest.translation),
            .001);
        EXPECT_LE(Math::length(coupled.value().local_transforms[1].translation
                               - data.nodes[1].rest.translation),
            body.max_translation + .0001);
        for(size_t i = 2; i < data.nodes.size(); ++i)
            EXPECT_EQ(
                coupled.value().local_transforms[i].translation, data.nodes[i].rest.translation);
        EXPECT_EQ(base.value().local_transforms[1].translation, data.nodes[1].rest.translation);
        AnimationPlayback motion_frame{.root_motion = AnimationRootMotion{.node = "pelvis"}};
        auto motion_base = sample_animation(data, &motion_frame);
        ASSERT_TRUE(motion_base);
        auto constrained_motion = apply_animation_constraints(
            data, motion_base.value(), constraints, &*motion_frame.root_motion, {}, &body);
        ASSERT_TRUE(constrained_motion) << constrained_motion.error();
        EXPECT_LT(error(constrained_motion.value()), error(independent.value()));
        auto presented = interpolate_animation_pose(data, constrained_motion.value(),
            constrained_motion.value(), .5f, &*motion_frame.root_motion);
        ASSERT_TRUE(presented);
        EXPECT_NEAR(error(presented.value()), error(constrained_motion.value()), .0001);
        body.weight = 0;
        auto zero =
            apply_animation_constraints(data, base.value(), constraints, nullptr, {}, &body);
        ASSERT_TRUE(zero);
        EXPECT_EQ(zero.value().matrices, independent.value().matrices);
        body.iterations = 0;
        EXPECT_FALSE(
            apply_animation_constraints(data, base.value(), constraints, nullptr, {}, &body));
    }

    TEST(AnimationIKTest, ReachesTargetPreservesBoneLengthsAndMatchesFootRotation) {
        const auto data = limb();
        auto pose = sample_animation(data, nullptr);
        ASSERT_TRUE(pose);
        const AnimationIKConstraint constraint{.tip = 3,
            .target = {.3f, -1.5f, .2f},
            .pole = {0, 0, 2},
            .rotation = Math::angle_axis(.2f, {0, 0, 1})};
        auto result = apply_animation_constraints(data, pose.value(), std::span(&constraint, 1));
        ASSERT_TRUE(result) << result.error();
        const auto& bones = result.value().bone_matrices;
        EXPECT_LT(Math::length(Math::Vec3(bones[2][3]) - constraint.target), .0001);
        EXPECT_NEAR(Math::length(Math::Vec3(bones[1][3] - bones[0][3])), 1, .0001);
        EXPECT_NEAR(Math::length(Math::Vec3(bones[2][3] - bones[1][3])), 1, .0001);
        EXPECT_GT(bones[1][3].z, .1);
        EXPECT_NEAR(std::abs(Math::dot(Math::to_quat(Math::Mat3(bones[2])), *constraint.rotation)),
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
            auto solved =
                apply_animation_constraints(data, pose.value(), std::span(&constraint, 1));
            ASSERT_TRUE(solved) << solved.error();
            const auto foot = Math::Vec3(solved.value().bone_matrices[2][3]);
            EXPECT_TRUE(Math::is_finite(foot));
            EXPECT_LE(Math::length(foot), 2.0001);
            if(Math::length(target) > 0)
                EXPECT_NEAR(Math::dot(Math::normalize(foot), Math::normalize(target)), 1, .0001);
        }
    }
    TEST(AnimationIKTest, PartialWeightsComposeInLocalRotationsAndInvalidInputsAreDiagnosed) {
        auto data = limb();
        const auto pose = sample_animation(data, nullptr);
        ASSERT_TRUE(pose);
        AnimationIKConstraint constraint{.tip = 3, .target = {0, -1.5f, .2f}, .pole = {0, 0, 1}};
        auto full = apply_animation_constraints(data, pose.value(), std::span(&constraint, 1));
        ASSERT_TRUE(full);
        constraint.weight = .5f;
        auto half = apply_animation_constraints(data, pose.value(), std::span(&constraint, 1));
        ASSERT_TRUE(half);
        EXPECT_NEAR(std::abs(Math::dot(half.value().local_transforms[1].rotation,
                        Math::slerp(data.nodes[1].rest.rotation,
                            full.value().local_transforms[1].rotation, .5f))),
            1, .0001);
        constraint.weight = 0;
        EXPECT_EQ(apply_animation_constraints(data, pose.value(), std::span(&constraint, 1))
                      .value()
                      .matrices,
            pose.value().matrices);
        constraint.weight = 1;
        constraint.tip = 1;
        EXPECT_FALSE(apply_animation_constraints(data, pose.value(), std::span(&constraint, 1)));
        constraint.tip = 3;
        constraint.target.x = NAN;
        EXPECT_FALSE(apply_animation_constraints(data, pose.value(), std::span(&constraint, 1)));
        constraint.target.x = 0;
        constraint.rotation = Math::Quat(0, 0, 0, 0);
        EXPECT_FALSE(apply_animation_constraints(data, pose.value(), std::span(&constraint, 1)));
        constraint.rotation.reset();
        auto foreign = std::make_shared<AnimationData>(data);
        AnimationPlayback frame{.ik = {constraint}, .pose_source = foreign};
        const auto stale = sample_animation(data, &frame);
        ASSERT_FALSE(stale);
        EXPECT_NE(stale.error().find("skeleton changed"), std::string::npos);
        data.nodes[1].rest.scale.x = 2;
        const auto scaled = sample_animation(data, nullptr);
        ASSERT_TRUE(scaled);
        EXPECT_FALSE(apply_animation_constraints(data, scaled.value(), std::span(&constraint, 1)));
    }

    TEST(AnimationIKTest, SceneReuseRequiresIdenticalSamplingAndCompatibleConstraints) {
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
