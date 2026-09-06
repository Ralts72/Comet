#include <gtest/gtest.h>

#include "render/scene/scene_picking.h"
#include "graphics/vk_common.h"
#include "../test_utils.h"

#include <array>
#include <algorithm>
#include <limits>

namespace Comet::Tests {
    namespace {
        BoundingBox unit_box() {
            return {.minimum = Math::Vec3(-1.0f), .maximum = Math::Vec3(1.0f)};
        }
    }

    TEST(ScenePickingTest, BuildsPerspectiveWorldRayThroughPixelCenter) {
        const ViewProjectMatrix view_project{
            .view = Math::look_at(Math::Vec3(0.0f, 0.0f, 3.0f), Math::Vec3(0.0f),
                Math::Vec3(0.0f, 1.0f, 0.0f)),
            .projection = Math::perspective(60.0f, 1.0f, 0.1f, 100.0f)};

        const auto ray = make_world_ray(view_project, Math::Vec2u(0), Math::Vec2u(1));

        ASSERT_TRUE(ray);
        EXPECT_NEAR(ray->origin.x, 0.0f, 0.0001f);
        EXPECT_NEAR(ray->origin.y, 0.0f, 0.0001f);
        EXPECT_NEAR(ray->origin.z, 2.9f, 0.0001f);
        EXPECT_TRUE(
            TestUtils::Vec3Equal(ray->direction, Math::Vec3(0.0f, 0.0f, -1.0f), 0.0001f));
        EXPECT_FALSE(make_world_ray(view_project, Math::Vec2u(1, 0), Math::Vec2u(1)));
    }

    TEST(ScenePickingTest, BuildsParallelOrthographicRaysAtPixelPositions) {
        const ViewProjectMatrix view_project{
            .view = Math::look_at(Math::Vec3(0.0f, 0.0f, 3.0f), Math::Vec3(0.0f),
                Math::Vec3(0.0f, 1.0f, 0.0f)),
            .projection = Math::ortho(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 100.0f)};

        const auto top_left =
            make_world_ray(view_project, Math::Vec2u(0, 0), Math::Vec2u(2, 2));
        const auto bottom_right =
            make_world_ray(view_project, Math::Vec2u(1, 1), Math::Vec2u(2, 2));

        ASSERT_TRUE(top_left);
        ASSERT_TRUE(bottom_right);
        EXPECT_NEAR(top_left->origin.x, -1.0f, 0.0001f);
        EXPECT_NEAR(top_left->origin.y, 0.5f, 0.0001f);
        EXPECT_NEAR(bottom_right->origin.x, 1.0f, 0.0001f);
        EXPECT_NEAR(bottom_right->origin.y, -0.5f, 0.0001f);
        EXPECT_TRUE(
            TestUtils::Vec3Equal(top_left->direction, bottom_right->direction, 0.0001f));
    }

    TEST(ScenePickingTest, PicksObjectsAtTheirRenderedPixelsInBothProjections) {
        const Math::Vec2u resolution(640, 480);
        const auto viewport = Graphics::get_viewport(resolution.x, resolution.y);
        const auto view =
            Math::look_at(Math::Vec3(0, 0, 5), Math::Vec3(0), Math::Vec3(0, 1, 0));
        const std::array positions{Math::Vec3(0, 1, 0), Math::Vec3(0, -1, 0)};
        std::array<ScenePickCandidate, 2> candidates;
        for(std::size_t i = 0; i < positions.size(); ++i) {
            candidates[i] = {.entity_id = static_cast<EntityId>(i + 1),
                .model_matrix = Math::translate(Math::Mat4(1), positions[i]),
                .local_bounds = {
                    .minimum = Math::Vec3(-0.1f), .maximum = Math::Vec3(0.1f)}};
        }
        for(const auto& projection : {Math::perspective(60.0f, 4.0f / 3.0f, 0.1f, 100.0f),
                Math::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 100.0f)}) {
            for(std::size_t i = 0; i < positions.size(); ++i) {
                const auto clip = projection * view * Math::Vec4(positions[i], 1);
                const auto ndc = Math::Vec3(clip) / clip.w;
                // 正向映射使用实际后端 Viewport，避免重复拾取公式中的假设。
                const Math::Vec2u pixel(
                    viewport.x + viewport.width * (ndc.x + 1.0f) * 0.5f,
                    viewport.y + viewport.height * (ndc.y + 1.0f) * 0.5f);
                const auto ray = make_world_ray(
                    {.view = view, .projection = projection}, pixel, resolution);
                ASSERT_TRUE(ray);
                const auto hit = pick_scene_candidates(*ray, candidates);
                ASSERT_TRUE(hit);
                EXPECT_EQ(hit->entity_id, candidates[i].entity_id);
            }
        }
    }

    TEST(ScenePickingTest, SelectsNearestTransformedCandidate) {
        const Ray ray{.origin = Math::Vec3(0.0f, 0.0f, 5.0f),
            .direction = Math::Vec3(0.0f, 0.0f, -1.0f)};
        const std::array candidates{ScenePickCandidate{.entity_id = 9,
                                        .model_matrix = Math::translate(Math::Mat4(1.0f),
                                            Math::Vec3(0.0f, 0.0f, -5.0f)),
                                        .local_bounds = unit_box()},
            ScenePickCandidate{.entity_id = 3,
                .model_matrix = Math::Mat4(1.0f),
                .local_bounds = unit_box()}};

        const auto hit = pick_scene_candidates(ray, candidates);

        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->entity_id, 3u);
        EXPECT_FLOAT_EQ(hit->distance, 4.0f);
    }

    TEST(ScenePickingTest, PreservesWorldDistanceUnderNonUniformAndNegativeScale) {
        const Ray ray{.origin = Math::Vec3(0.0f, 0.0f, 10.0f),
            .direction = Math::Vec3(0.0f, 0.0f, -1.0f)};
        const std::array candidates{
            ScenePickCandidate{.entity_id = 1, .local_bounds = unit_box()},
            ScenePickCandidate{.entity_id = 2,
                .model_matrix =
                    Math::scale(Math::Mat4(1.0f), Math::Vec3(-2.0f, 3.0f, 4.0f)),
                .local_bounds = unit_box()},
        };
        const auto hit = pick_scene_candidates(ray, candidates);
        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->entity_id, 2u);
        EXPECT_FLOAT_EQ(hit->distance, 6.0f);
    }

    TEST(ScenePickingTest, PicksLargeScaledMeshesWithoutTreatingDirectionAsZero) {
        const Ray ray{.origin = Math::Vec3(0.0f, 0.0f, 2.0e8f),
            .direction = Math::Vec3(0.0f, 0.0f, -1.0f)};
        const std::array candidates{
            ScenePickCandidate{.entity_id = 1,
                .model_matrix = Math::scale(Math::Mat4(1.0f), Math::Vec3(1.0e8f)),
                .local_bounds = unit_box()},
        };
        const auto hit = pick_scene_candidates(ray, candidates);
        ASSERT_TRUE(hit);
        EXPECT_FLOAT_EQ(hit->distance, 1.0e8f);
    }

    TEST(ScenePickingTest, ClipsPickRayToNearAndFarPlanes) {
        const ViewProjectMatrix view_project{.view = Math::Mat4(1.0f),
            .projection = Math::perspective(60.0f, 1.0f, 1.0f, 10.0f)};
        const auto ray = make_world_ray(view_project, Math::Vec2u(0), Math::Vec2u(1));
        ASSERT_TRUE(ray);
        EXPECT_NEAR(ray->max_parameter, 9.0f, 0.0001f);
        const std::array candidates{
            ScenePickCandidate{.entity_id = 1,
                .model_matrix = Math::translate(Math::Mat4(1.0f), Math::Vec3(0, 0, -20)),
                .local_bounds = unit_box()},
            ScenePickCandidate{.entity_id = 2,
                .model_matrix = Math::scale(Math::Mat4(1.0f), Math::Vec3(0.1f)),
                .local_bounds = unit_box()},
        };
        EXPECT_FALSE(pick_scene_candidates(*ray, candidates));
    }

    TEST(ScenePickingTest, RejectsInvalidViewProjectionAndResolution) {
        ViewProjectMatrix view_project{
            .view = Math::Mat4(1), .projection = Math::Mat4(0)};
        EXPECT_FALSE(make_world_ray(view_project, Math::Vec2u(0), Math::Vec2u(1)));
        view_project.projection = Math::Mat4(1);
        EXPECT_FALSE(make_world_ray(view_project, Math::Vec2u(0), Math::Vec2u(0)));
        EXPECT_FALSE(make_world_ray(view_project, Math::Vec2u(0, 1), Math::Vec2u(1)));
        view_project.view[0][0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(make_world_ray(view_project, Math::Vec2u(0), Math::Vec2u(1)));
        EXPECT_FALSE(pick_render_submission({}, Math::Vec2u(0), Math::Vec2u(1)));
    }

    TEST(ScenePickingTest, EqualDistanceChoiceDoesNotDependOnCandidateOrder) {
        const Ray ray{.origin = Math::Vec3(0, 0, 5), .direction = Math::Vec3(0, 0, -1)};
        std::array candidates{
            ScenePickCandidate{.entity_id = 9, .local_bounds = unit_box()},
            ScenePickCandidate{.entity_id = 3, .local_bounds = unit_box()},
        };
        const auto first = pick_scene_candidates(ray, candidates);
        std::reverse(candidates.begin(), candidates.end());
        const auto reversed = pick_scene_candidates(ray, candidates);
        ASSERT_TRUE(first);
        ASSERT_TRUE(reversed);
        EXPECT_EQ(first->entity_id, 3u);
        EXPECT_EQ(first->entity_id, reversed->entity_id);
    }

    TEST(ScenePickingTest, IgnoresMissesInvalidIdsAndSingularModels) {
        const Ray ray{.origin = Math::Vec3(0.0f, 0.0f, 5.0f),
            .direction = Math::Vec3(0.0f, 0.0f, -1.0f)};
        const std::array candidates{ScenePickCandidate{.entity_id = INVALID_ENTITY_ID,
                                        .local_bounds = unit_box()},
            ScenePickCandidate{.entity_id = 7,
                .model_matrix = Math::scale(Math::Mat4(1.0f), Math::Vec3(0.0f)),
                .local_bounds = unit_box()},
            ScenePickCandidate{.entity_id = 8,
                .model_matrix =
                    Math::translate(Math::Mat4(1.0f), Math::Vec3(5.0f, 0.0f, 0.0f)),
                .local_bounds = unit_box()}};

        EXPECT_FALSE(pick_scene_candidates(ray, candidates));
    }
}
