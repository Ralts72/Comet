#include <gtest/gtest.h>

#include "core/geometry.h"

#include <limits>

namespace Comet::Tests {
    namespace {
        const AxisAlignedBox UNIT_BOX{
            .minimum = Math::Vec3(-1.0f),
            .maximum = Math::Vec3(1.0f)
        };
    }

    TEST(RayBoxTest, ReturnsNearestNonNegativeIntersection) {
        const auto hit = intersect_ray_box(
            {
                .origin = Math::Vec3(0.0f, 0.0f, 5.0f),
                .direction = Math::Vec3(0.0f, 0.0f, -1.0f)
            },
            UNIT_BOX);

        ASSERT_TRUE(hit);
        EXPECT_FLOAT_EQ(*hit, 4.0f);
    }

    TEST(RayBoxTest, HandlesParallelMissAndOriginInside) {
        EXPECT_FALSE(intersect_ray_box(
            {
                .origin = Math::Vec3(2.0f, 0.0f, 5.0f),
                .direction = Math::Vec3(0.0f, 0.0f, -1.0f)
            },
            UNIT_BOX));

        const auto inside = intersect_ray_box(
            {
                .origin = Math::Vec3(0.0f),
                .direction = Math::Vec3(1.0f, 0.0f, 0.0f)
            },
            UNIT_BOX);
        ASSERT_TRUE(inside);
        EXPECT_FLOAT_EQ(*inside, 0.0f);
    }

    TEST(RayBoxTest, RejectsInvalidRayOrBounds) {
        EXPECT_FALSE(intersect_ray_box(
            {.origin = Math::Vec3(0.0f), .direction = Math::Vec3(0.0f)},
            UNIT_BOX));

        AxisAlignedBox invalid = UNIT_BOX;
        invalid.maximum.x = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(intersect_ray_box(
            {
                .origin = Math::Vec3(0.0f, 0.0f, 5.0f),
                .direction = Math::Vec3(0.0f, 0.0f, -1.0f)
            },
            invalid));
    }

    TEST(AxisAlignedBoxTest, TransformsAllCornersIntoWorldBounds) {
        const AxisAlignedBox local{
            .minimum = Math::Vec3(-1.0f, -2.0f, -0.5f),
            .maximum = Math::Vec3(1.0f, 2.0f, 0.5f)
        };
        Math::Mat4 transform = Math::translate(
            Math::Mat4(1.0f), Math::Vec3(3.0f, -1.0f, 2.0f));
        transform = Math::rotate(
            transform,
            Math::radians(90.0f),
            Math::Vec3(0.0f, 0.0f, 1.0f));
        transform = Math::scale(
            transform, Math::Vec3(3.0f, 1.0f, 1.0f));

        const auto world = transform_box(local, transform);

        ASSERT_TRUE(world);
        EXPECT_NEAR(world->center().x, 3.0f, 0.0001f);
        EXPECT_NEAR(world->center().y, -1.0f, 0.0001f);
        EXPECT_NEAR(world->center().z, 2.0f, 0.0001f);
        EXPECT_NEAR(world->size().x, 4.0f, 0.0001f);
        EXPECT_NEAR(world->size().y, 6.0f, 0.0001f);
        EXPECT_NEAR(world->size().z, 1.0f, 0.0001f);
    }

    TEST(AxisAlignedBoxTest, RejectsNonFiniteTransform) {
        Math::Mat4 transform(1.0f);
        transform[0][0] = std::numeric_limits<float>::quiet_NaN();

        EXPECT_FALSE(transform_box(UNIT_BOX, transform));
    }
}
