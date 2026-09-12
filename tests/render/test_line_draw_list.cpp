#include "render/line_draw_list.h"

#include <gtest/gtest.h>

#include <limits>

namespace Comet::Tests {
    class LineDrawListTest: public ::testing::Test {
    protected:
        LineDrawList list;
    };

    TEST_F(LineDrawListTest, StoresWorldSpaceEndpointsAndColor) {
        const Math::Vec3 start(1, 2, 3);
        const Math::Vec3 end(4, 5, 6);
        const Math::Vec4 color(1, 0.5f, 0, 0.25f);
        ASSERT_TRUE(list.add_line(start, end, color));
        ASSERT_EQ(list.vertices().size(), 2U);
        EXPECT_EQ(list.vertices()[0].position, start);
        EXPECT_EQ(list.vertices()[1].position, end);
        EXPECT_EQ(list.vertices()[0].color, color);
        EXPECT_EQ(list.vertices()[1].color, color);
    }

    TEST_F(LineDrawListTest, RejectsInvalidRequestsWithoutChangingExistingLines) {
        ASSERT_TRUE(list.add_line({0, 0, 0}, {1, 0, 0}));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(list.add_line({nan, 0, 0}, {1, 0, 0}));
        EXPECT_FALSE(list.add_line({0, 0, 0}, {1, inf, 0}));
        EXPECT_FALSE(list.add_line({0, 0, 0}, {1, 0, 0}, {1, 1, 1, nan}));
        EXPECT_FALSE(list.add_box({.minimum = {1, 1, 1}, .maximum = {0, 0, 0}}));
        EXPECT_FALSE(list.add_box({.minimum = {0, 0, 0}, .maximum = {inf, 1, 1}}));
        EXPECT_FALSE(list.add_box({}, {1, 1, inf, 1}));
        EXPECT_EQ(list.line_count(), 1U);
        EXPECT_EQ(list.vertices()[1].position, Math::Vec3(1, 0, 0));
    }

    TEST_F(LineDrawListTest, BoxContainsTwelveUniqueEdgesWithoutFaceDiagonals) {
        const BoundingBox box{.minimum = {-1, -2, -3}, .maximum = {4, 5, 6}};
        const Math::Vec4 color(0, 1, 0, 1);
        ASSERT_TRUE(list.add_box(box, color));
        ASSERT_EQ(list.line_count(), 12U);
        const auto vertices = list.vertices();
        for(std::size_t index = 0; index < vertices.size(); index += 2) {
            const auto start = vertices[index].position;
            const auto end = vertices[index + 1].position;
            int changed_axes = 0;
            for(int axis = 0; axis < 3; ++axis) {
                EXPECT_TRUE(
                    start[axis] == box.minimum[axis] || start[axis] == box.maximum[axis]);
                EXPECT_TRUE(
                    end[axis] == box.minimum[axis] || end[axis] == box.maximum[axis]);
                changed_axes += start[axis] != end[axis];
            }
            EXPECT_EQ(changed_axes, 1);
            EXPECT_EQ(vertices[index].color, color);
            EXPECT_EQ(vertices[index + 1].color, color);
            for(std::size_t previous = 0; previous < index; previous += 2) {
                const auto a = vertices[previous].position;
                const auto b = vertices[previous + 1].position;
                EXPECT_FALSE((a == start && b == end) || (a == end && b == start));
            }
        }
    }

    TEST_F(LineDrawListTest, AppendsOwnedCopiesAndCanAppendItself) {
        LineDrawList other;
        ASSERT_TRUE(other.add_line({1, 2, 3}, {4, 5, 6}));
        list.append(other);
        other.clear();
        list.append(list);
        ASSERT_EQ(list.line_count(), 2U);
        EXPECT_EQ(list.vertices()[0].position, list.vertices()[2].position);
        EXPECT_EQ(list.vertices()[1].position, list.vertices()[3].position);
        list.append(other);
        EXPECT_EQ(list.line_count(), 2U);
        list.clear();
        EXPECT_TRUE(list.empty());
        EXPECT_TRUE(list.add_box(BoundingBox::from_point({1, 1, 1})));
        EXPECT_EQ(list.line_count(), 12U);
    }

    TEST_F(LineDrawListTest, TransformedBoxPreservesEdgesUnderRotationScaleAndShear) {
        const BoundingBox box{.minimum = {-1, -2, -3}, .maximum = {1, 2, 3}};
        const auto parent = Math::compose_trs({3, 4, 5}, {0, 35, 0}, {-2, 1, 0.5f});
        const auto local = Math::compose_trs({1, 0, 2}, {20, 0, 30}, {1, 2, 3});
        const auto transform = parent * local;
        LineDrawList local_lines;
        ASSERT_TRUE(local_lines.add_box(box));
        ASSERT_TRUE(list.add_box(box, transform, Math::Vec4(1, 0, 0, 1)));
        ASSERT_EQ(list.vertices().size(), local_lines.vertices().size());
        for(std::size_t index = 0; index < list.vertices().size(); ++index) {
            const auto expected = Math::Vec3(
                transform * Math::Vec4(local_lines.vertices()[index].position, 1));
            EXPECT_LT(Math::length(list.vertices()[index].position - expected), 0.0001f);
            EXPECT_EQ(list.vertices()[index].color, Math::Vec4(1, 0, 0, 1));
        }
    }

    TEST_F(LineDrawListTest, RejectsInvalidTransformWithoutPartialBox) {
        ASSERT_TRUE(list.add_line({0, 0, 0}, {1, 0, 0}));
        const BoundingBox box{.minimum = {-1, -1, -1}, .maximum = {1, 1, 1}};
        auto transform = Math::Mat4(1);
        transform[0][3] = 0.5f;
        EXPECT_FALSE(list.add_box(box, transform));
        transform = Math::Mat4(1);
        transform[3][0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(list.add_box(box, transform));
        transform = Math::Mat4(std::numeric_limits<float>::max());
        transform[3][3] = 1;
        EXPECT_FALSE(
            list.add_box({.minimum = {0, 0, 0}, .maximum = {2, 2, 2}}, transform));
        EXPECT_EQ(list.line_count(), 1U);
    }

    TEST_F(LineDrawListTest, CollapsedScaleProducesFiniteDegenerateEdges) {
        const auto transform = Math::compose_trs({1, 2, 3}, {0, 0, 0}, {0, 0, 0});
        ASSERT_TRUE(
            list.add_box({.minimum = {-1, -1, -1}, .maximum = {1, 1, 1}}, transform));
        ASSERT_EQ(list.line_count(), 12U);
        for(const auto& vertex : list.vertices()) {
            EXPECT_EQ(vertex.position, Math::Vec3(1, 2, 3));
        }
    }
}
