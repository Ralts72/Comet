#include "render/resource/mesh_data.h"
#include "../test_utils.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    TEST(MeshBoundsTest, CalculatesFiniteLocalAxisAlignedBounds) {
        MeshData data;
        data.vertices = {
            {.position = Math::Vec3(-2.0f, 4.0f, 1.0f)},
            {.position = Math::Vec3(3.0f, -1.0f, 5.0f)},
            {.position = Math::Vec3(0.0f, 2.0f, -4.0f)},
        };

        const auto bounds = calculate_mesh_bounds(data);

        ASSERT_TRUE(bounds);
        EXPECT_TRUE(bounds->is_valid());
        EXPECT_TRUE(
            TestUtils::Vec3Equal(bounds->minimum, Math::Vec3(-2.0f, -1.0f, -4.0f)));
        EXPECT_TRUE(TestUtils::Vec3Equal(bounds->maximum, Math::Vec3(3.0f, 4.0f, 5.0f)));
        EXPECT_TRUE(TestUtils::Vec3Equal(bounds->center(), Math::Vec3(0.5f, 1.5f, 0.5f)));
        EXPECT_TRUE(TestUtils::Vec3Equal(bounds->size(), Math::Vec3(5.0f, 5.0f, 9.0f)));
    }

    TEST(MeshBoundsTest, DoesNotIncludeOriginOrRequireIndices) {
        MeshData data;
        data.vertices = {{.position = Math::Vec3(2.0f, 3.0f, 4.0f)}};
        const auto point_bounds = calculate_mesh_bounds(data);
        ASSERT_TRUE(point_bounds);
        EXPECT_TRUE(
            TestUtils::Vec3Equal(point_bounds->minimum, data.vertices[0].position));
        EXPECT_TRUE(
            TestUtils::Vec3Equal(point_bounds->maximum, data.vertices[0].position));
        EXPECT_TRUE(TestUtils::Vec3Equal(point_bounds->size(), Math::Vec3(0.0f)));

        data.vertices.push_back({.position = Math::Vec3(5.0f, 6.0f, 7.0f)});
        data.indices = {0};
        const auto bounds = calculate_mesh_bounds(data);
        ASSERT_TRUE(bounds);
        EXPECT_TRUE(TestUtils::Vec3Equal(bounds->minimum, data.vertices[0].position));
        EXPECT_TRUE(TestUtils::Vec3Equal(bounds->maximum, data.vertices[1].position));
    }

    TEST(MeshBoundsTest, RejectsEmptyOrNonFinitePositions) {
        EXPECT_FALSE(calculate_mesh_bounds({}));
        for(const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity()}) {
            for(int axis = 0; axis < 3; ++axis) {
                MeshData data;
                data.vertices = {{.position = Math::Vec3(1.0f)}};
                data.vertices.front().position[axis] = invalid;
                EXPECT_FALSE(calculate_mesh_bounds(data));

                data.vertices.insert(
                    data.vertices.begin(), {.position = Math::Vec3(0.0f)});
                EXPECT_FALSE(calculate_mesh_bounds(data));
            }
        }
    }

    TEST(MeshBoundsTest, CenterDoesNotOverflowForLargeFiniteCoordinates) {
        const float maximum = std::numeric_limits<float>::max();
        const BoundingBox box{
            .minimum = Math::Vec3(maximum * 0.5f), .maximum = Math::Vec3(maximum)};
        ASSERT_TRUE(box.is_valid());
        const auto center = box.center();
        EXPECT_TRUE(std::isfinite(center.x));
        EXPECT_TRUE(std::isfinite(center.y));
        EXPECT_TRUE(std::isfinite(center.z));
        EXPECT_FLOAT_EQ(center.x, maximum * 0.75f);
    }

    TEST(MeshBoundsTest, RejectsInvertedOrNonFiniteBoxes) {
        EXPECT_FALSE(
            (BoundingBox{.minimum = Math::Vec3(1.0f), .maximum = Math::Vec3(-1.0f)})
                .is_valid());
        EXPECT_FALSE(
            BoundingBox::from_point(Math::Vec3(std::numeric_limits<float>::infinity()))
                .is_valid());
    }
}
