#include <gtest/gtest.h>

#include "render/resource/mesh_data.h"
#include "../test_utils.h"

#include <limits>

namespace Comet::Tests {
    TEST(MeshBoundsTest, CalculatesFiniteLocalAxisAlignedBounds) {
        MeshData data;
        data.vertices = {
            {.position = Math::Vec3(-2.0f, 4.0f, 1.0f)},
            {.position = Math::Vec3(3.0f, -1.0f, 5.0f)},
            {.position = Math::Vec3(0.0f, 2.0f, -4.0f)}
        };

        const auto bounds = calculate_mesh_bounds(data);

        ASSERT_TRUE(bounds);
        EXPECT_TRUE(bounds->is_valid());
        EXPECT_TRUE(TestUtils::Vec3Equal(
            bounds->minimum, Math::Vec3(-2.0f, -1.0f, -4.0f)));
        EXPECT_TRUE(TestUtils::Vec3Equal(
            bounds->maximum, Math::Vec3(3.0f, 4.0f, 5.0f)));
        EXPECT_TRUE(TestUtils::Vec3Equal(
            bounds->center(), Math::Vec3(0.5f, 1.5f, 0.5f)));
        EXPECT_TRUE(TestUtils::Vec3Equal(
            bounds->size(), Math::Vec3(5.0f, 5.0f, 9.0f)));
    }

    TEST(MeshBoundsTest, RejectsEmptyOrNonFinitePositions) {
        EXPECT_FALSE(calculate_mesh_bounds({}));

        MeshData invalid;
        invalid.vertices = {{
            .position = Math::Vec3(
                0.0f,
                std::numeric_limits<float>::quiet_NaN(),
                0.0f)
        }};
        EXPECT_FALSE(calculate_mesh_bounds(invalid));
    }
}
