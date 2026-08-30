#include <gtest/gtest.h>

#include "render/scene/scene_picking.h"
#include "../test_utils.h"

#include <array>

namespace Comet::Tests {
    namespace {
        AxisAlignedBox unit_box() {
            return {
                .minimum = Math::Vec3(-1.0f),
                .maximum = Math::Vec3(1.0f)
            };
        }
    }

    TEST(ScenePickingTest, BuildsPerspectiveWorldRayThroughPixelCenter) {
        const ViewProjectMatrix view_project{
            .view = Math::look_at(
                Math::Vec3(0.0f, 0.0f, 3.0f),
                Math::Vec3(0.0f),
                Math::Vec3(0.0f, 1.0f, 0.0f)),
            .projection = Math::perspective(60.0f, 1.0f, 0.1f, 100.0f)
        };

        const auto ray = make_world_ray(
            view_project, Math::Vec2u(0), Math::Vec2u(1));

        ASSERT_TRUE(ray);
        EXPECT_NEAR(ray->origin.x, 0.0f, 0.0001f);
        EXPECT_NEAR(ray->origin.y, 0.0f, 0.0001f);
        EXPECT_NEAR(ray->origin.z, 2.9f, 0.0001f);
        EXPECT_TRUE(TestUtils::Vec3Equal(
            ray->direction, Math::Vec3(0.0f, 0.0f, -1.0f), 0.0001f));
        EXPECT_FALSE(make_world_ray(
            view_project, Math::Vec2u(1, 0), Math::Vec2u(1)));
    }

    TEST(ScenePickingTest, BuildsParallelOrthographicRaysAtPixelPositions) {
        const ViewProjectMatrix view_project{
            .view = Math::look_at(
                Math::Vec3(0.0f, 0.0f, 3.0f),
                Math::Vec3(0.0f),
                Math::Vec3(0.0f, 1.0f, 0.0f)),
            .projection = Math::ortho(
                -2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 100.0f)
        };

        const auto top_left = make_world_ray(
            view_project, Math::Vec2u(0, 0), Math::Vec2u(2, 2));
        const auto bottom_right = make_world_ray(
            view_project, Math::Vec2u(1, 1), Math::Vec2u(2, 2));

        ASSERT_TRUE(top_left);
        ASSERT_TRUE(bottom_right);
        EXPECT_NEAR(top_left->origin.x, -1.0f, 0.0001f);
        EXPECT_NEAR(top_left->origin.y, -0.5f, 0.0001f);
        EXPECT_NEAR(bottom_right->origin.x, 1.0f, 0.0001f);
        EXPECT_NEAR(bottom_right->origin.y, 0.5f, 0.0001f);
        EXPECT_TRUE(TestUtils::Vec3Equal(
            top_left->direction, bottom_right->direction, 0.0001f));
    }

    TEST(ScenePickingTest, SelectsNearestTransformedCandidate) {
        const Ray ray{
            .origin = Math::Vec3(0.0f, 0.0f, 5.0f),
            .direction = Math::Vec3(0.0f, 0.0f, -1.0f)
        };
        const std::array candidates{
            ScenePickCandidate{
                .entity_id = 9,
                .model_matrix = Math::translate(
                    Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -5.0f)),
                .local_bounds = unit_box()
            },
            ScenePickCandidate{
                .entity_id = 3,
                .model_matrix = Math::Mat4(1.0f),
                .local_bounds = unit_box()
            }
        };

        const auto hit = pick_scene_candidates(ray, candidates);

        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->entity_id, 3u);
        EXPECT_FLOAT_EQ(hit->distance, 4.0f);
    }

    TEST(ScenePickingTest, ResolvesEqualDistanceByStableEntityId) {
        const Ray ray{
            .origin = Math::Vec3(0.0f, 0.0f, 5.0f),
            .direction = Math::Vec3(0.0f, 0.0f, -1.0f)
        };
        const std::array candidates{
            ScenePickCandidate{
                .entity_id = 9,
                .local_bounds = unit_box()
            },
            ScenePickCandidate{
                .entity_id = 3,
                .local_bounds = unit_box()
            }
        };

        const auto hit = pick_scene_candidates(ray, candidates);

        ASSERT_TRUE(hit);
        EXPECT_EQ(hit->entity_id, 3u);
    }

    TEST(ScenePickingTest, IgnoresMissesInvalidIdsAndSingularModels) {
        const Ray ray{
            .origin = Math::Vec3(0.0f, 0.0f, 5.0f),
            .direction = Math::Vec3(0.0f, 0.0f, -1.0f)
        };
        const std::array candidates{
            ScenePickCandidate{
                .entity_id = INVALID_ENTITY_ID,
                .local_bounds = unit_box()
            },
            ScenePickCandidate{
                .entity_id = 7,
                .model_matrix = Math::scale(
                    Math::Mat4(1.0f), Math::Vec3(0.0f)),
                .local_bounds = unit_box()
            },
            ScenePickCandidate{
                .entity_id = 8,
                .model_matrix = Math::translate(
                    Math::Mat4(1.0f), Math::Vec3(5.0f, 0.0f, 0.0f)),
                .local_bounds = unit_box()
            }
        };

        EXPECT_FALSE(pick_scene_candidates(ray, candidates));
    }
}
