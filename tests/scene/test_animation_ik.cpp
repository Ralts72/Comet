#include "animation/animation_data.h"
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
}
