#include <gtest/gtest.h>

#include "core/geometry.h"

#include <limits>

namespace Comet::Tests {
    namespace {
        const BoundingBox UNIT_BOX{.minimum = Math::Vec3(-1.0f), .maximum = Math::Vec3(1.0f)};
    }

    TEST(BoundingBoxTest, TransformsAllCornersIntoWorldBounds) {
        const BoundingBox local{
            .minimum = Math::Vec3(-1.0f, -2.0f, -0.5f), .maximum = Math::Vec3(1.0f, 2.0f, 0.5f)};
        Math::Mat4 transform = Math::translate(Math::Mat4(1.0f), Math::Vec3(3.0f, -1.0f, 2.0f));
        transform = Math::rotate(transform, Math::radians(90.0f), Math::Vec3(0.0f, 0.0f, 1.0f));
        transform = Math::scale(transform, Math::Vec3(-3.0f, 1.0f, 1.0f));

        const auto world = transform_box(local, transform);

        ASSERT_TRUE(world);
        EXPECT_NEAR(world->center().x, 3.0f, 0.0001f);
        EXPECT_NEAR(world->center().y, -1.0f, 0.0001f);
        EXPECT_NEAR(world->center().z, 2.0f, 0.0001f);
        EXPECT_NEAR(world->size().x, 4.0f, 0.0001f);
        EXPECT_NEAR(world->size().y, 6.0f, 0.0001f);
        EXPECT_NEAR(world->size().z, 1.0f, 0.0001f);

        // 与独立的八角点参考比较，覆盖非对称界限、错切和负缩放。
        for(int sample = 0; sample < 32; ++sample) {
            transform = Math::compose_trs(
                {float(sample), -3, 2}, {13, float(sample * 11), 29}, {-0.7f, 2.3f, 0.4f});
            transform[1][0] += 0.6f;
            const BoundingBox asymmetric{{-3, -2, -1}, {1, 4, 2}};
            auto reference =
                BoundingBox::from_point(Math::Vec3(transform * Math::Vec4(asymmetric.minimum, 1)));
            for(int corner = 0; corner < 8; ++corner) {
                auto point = asymmetric.minimum;
                for(int axis = 0; axis < 3; ++axis)
                    if(corner & (1 << axis))
                        point[axis] = asymmetric.maximum[axis];
                reference.include(Math::Vec3(transform * Math::Vec4(point, 1)));
            }
            const auto actual = transform_box(asymmetric, transform);
            ASSERT_TRUE(actual);
            for(int axis = 0; axis < 3; ++axis) {
                EXPECT_NEAR(actual->minimum[axis], reference.minimum[axis], 0.00001f);
                EXPECT_NEAR(actual->maximum[axis], reference.maximum[axis], 0.00001f);
            }
        }
    }

    TEST(BoundingBoxTest, RejectsNonFiniteTransform) {
        Math::Mat4 transform(1.0f);
        transform[0][0] = std::numeric_limits<float>::quiet_NaN();

        EXPECT_FALSE(transform_box(UNIT_BOX, transform));
    }

    TEST(BoundingBoxTest, TransformRejectsProjectiveAndOverflowButAllowsZeroScale) {
        Math::Mat4 transform(1.0f);
        transform[0][3] = 2.0f;
        EXPECT_FALSE(transform_box(UNIT_BOX, transform));
        transform = Math::scale(Math::Mat4(1.0f), Math::Vec3(0.0f));
        const auto collapsed = transform_box(UNIT_BOX, transform);
        ASSERT_TRUE(collapsed);
        EXPECT_EQ(collapsed->size(), Math::Vec3(0.0f));
        transform = Math::Mat4(1.0f);
        transform[0][0] = std::numeric_limits<float>::max();
        EXPECT_FALSE(
            transform_box({.minimum = Math::Vec3(-2.0f), .maximum = Math::Vec3(2.0f)}, transform));
    }

    TEST(RayBoxTest, ReturnsNearestNonNegativeIntersection) {
        const auto hit = intersect_ray_box(
            {.origin = Math::Vec3(0.0f, 0.0f, 5.0f), .direction = Math::Vec3(0.0f, 0.0f, -1.0f)},
            UNIT_BOX);

        ASSERT_TRUE(hit);
        EXPECT_FLOAT_EQ(*hit, 4.0f);
    }

    TEST(FrustumTest, UsesZeroToOneDepthAndKeepsBoundaryBoxes) {
        const auto frustum = Frustum::from_view_projection(Math::Mat4(1));
        ASSERT_TRUE(frustum);
        EXPECT_TRUE(frustum->intersects(UNIT_BOX));
        for(const auto point :
            {Math::Vec3{-1, 0, 0.5f}, Math::Vec3{1, 0, 0.5f}, Math::Vec3{0, -1, 0.5f},
                Math::Vec3{0, 1, 0.5f}, Math::Vec3{0, 0, 0}, Math::Vec3{0, 0, 1}})
            EXPECT_TRUE(frustum->intersects(BoundingBox::from_point(point)));
        for(const auto point :
            {Math::Vec3{-2, 0, 0.5f}, Math::Vec3{2, 0, 0.5f}, Math::Vec3{0, -2, 0.5f},
                Math::Vec3{0, 2, 0.5f}, Math::Vec3{0, 0, -0.1f}, Math::Vec3{0, 0, 1.1f}})
            EXPECT_FALSE(frustum->intersects(BoundingBox::from_point(point)));
        const BoundingBox crossing{{0.9f, -0.1f, 0.1f}, {1.2f, 0.1f, 0.2f}};
        EXPECT_TRUE(frustum->intersects(crossing));
    }

    TEST(FrustumTest, HandlesPerspectiveOrthographicAndMovingCamera) {
        for(const auto projection :
            {Math::perspective(90, 1, 1, 10), Math::ortho(-2, 2, -2, 2, 1, 10)}) {
            const auto view = Math::look_at({3, 4, 5}, {3, 4, 0}, {0, 1, 0});
            const auto frustum = Frustum::from_view_projection(projection * view);
            ASSERT_TRUE(frustum);
            EXPECT_TRUE(frustum->intersects(BoundingBox::from_point({3, 4, 2})));
            EXPECT_FALSE(frustum->intersects(BoundingBox::from_point({3, 4, 6})));
            EXPECT_FALSE(frustum->intersects(BoundingBox::from_point({3, 4, -6})));
            EXPECT_FALSE(frustum->intersects(BoundingBox::from_point({30, 4, 2})));
        }
        EXPECT_FALSE(Frustum::from_view_projection(Math::Mat4(0)));
        auto invalid = Math::Mat4(1);
        invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(Frustum::from_view_projection(invalid));
        const auto frustum = Frustum::from_view_projection(Math::Mat4(1));
        ASSERT_TRUE(frustum);
        EXPECT_TRUE(frustum->intersects({Math::Vec3(1), Math::Vec3(-1)}));
    }

    TEST(RayBoxTest, HandlesParallelMissAndOriginInside) {
        EXPECT_FALSE(intersect_ray_box(
            {.origin = Math::Vec3(2.0f, 0.0f, 5.0f), .direction = Math::Vec3(0.0f, 0.0f, -1.0f)},
            UNIT_BOX));

        const auto inside = intersect_ray_box(
            {.origin = Math::Vec3(0.0f), .direction = Math::Vec3(1.0f, 0.0f, 0.0f)}, UNIT_BOX);
        ASSERT_TRUE(inside);
        EXPECT_FLOAT_EQ(*inside, 0.0f);
    }

    TEST(RayBoxTest, PreservesSmallDirectionsAndParameterLimit) {
        const auto hit = intersect_ray_box(
            {.origin = Math::Vec3(0.0f, 0.0f, 2.0f), .direction = Math::Vec3(0.0f, 0.0f, -1.0e-8f)},
            UNIT_BOX);
        ASSERT_TRUE(hit);
        EXPECT_NEAR(*hit, 1.0e8f, 10.0f);
        EXPECT_FALSE(intersect_ray_box({.origin = Math::Vec3(0.0f, 0.0f, 5.0f),
                                           .direction = Math::Vec3(0.0f, 0.0f, -1.0f),
                                           .max_parameter = 3.0f},
            UNIT_BOX));
        EXPECT_FALSE(intersect_ray_box(
            {.origin = Math::Vec3(0.0f, 0.0f, -5.0f), .direction = Math::Vec3(0.0f, 0.0f, -1.0f)},
            UNIT_BOX));
    }

    TEST(RayBoxTest, RejectsInvalidRayOrBounds) {
        EXPECT_FALSE(intersect_ray_box(
            {.origin = Math::Vec3(0.0f), .direction = Math::Vec3(0.0f)}, UNIT_BOX));

        BoundingBox invalid = UNIT_BOX;
        invalid.maximum.x = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(intersect_ray_box(
            {.origin = Math::Vec3(0.0f, 0.0f, 5.0f), .direction = Math::Vec3(0.0f, 0.0f, -1.0f)},
            invalid));
    }

    TEST(BoundingBoxTest, CenterDoesNotOverflowForLargeFiniteCoordinates) {
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

    TEST(BoundingBoxTest, RejectsInvertedOrNonFiniteBoxes) {
        EXPECT_FALSE(
            (BoundingBox{.minimum = Math::Vec3(1.0f), .maximum = Math::Vec3(-1.0f)}).is_valid());
        EXPECT_FALSE(
            BoundingBox::from_point(Math::Vec3(std::numeric_limits<float>::infinity())).is_valid());
    }
}
