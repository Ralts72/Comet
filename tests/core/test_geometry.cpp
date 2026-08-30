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
}
