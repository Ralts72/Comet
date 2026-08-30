#include "render/debug/debug_draw.h"

#include <gtest/gtest.h>

#include <limits>

namespace Comet::Tests {
    TEST(DebugDrawListTest, AddsColoredLineAsTwoVertices) {
        DebugDrawList draw_list;
        const Math::Vec4 color(1.0f, 0.5f, 0.25f, 0.75f);

        ASSERT_TRUE(draw_list.add_line(
            Math::Vec3(-1.0f, 2.0f, 3.0f),
            Math::Vec3(4.0f, 5.0f, -6.0f),
            color));

        ASSERT_EQ(draw_list.line_count(), 1u);
        ASSERT_EQ(draw_list.vertices().size(), 2u);
        EXPECT_EQ(
            draw_list.vertices()[0].position,
            Math::Vec3(-1.0f, 2.0f, 3.0f));
        EXPECT_EQ(
            draw_list.vertices()[1].position,
            Math::Vec3(4.0f, 5.0f, -6.0f));
        EXPECT_EQ(draw_list.vertices()[0].color, color);
        EXPECT_EQ(draw_list.vertices()[1].color, color);
    }

    TEST(DebugDrawListTest, BuildsTwelveDeterministicBoxEdges) {
        DebugDrawList draw_list;
        const AxisAlignedBox box{
            .minimum = Math::Vec3(-1.0f, -2.0f, -3.0f),
            .maximum = Math::Vec3(4.0f, 5.0f, 6.0f)
        };

        ASSERT_TRUE(draw_list.add_box(box, Math::Vec4(1.0f)));

        ASSERT_EQ(draw_list.line_count(), 12u);
        ASSERT_EQ(draw_list.vertices().size(), 24u);
        EXPECT_EQ(draw_list.vertices()[0].position, box.minimum);
        EXPECT_EQ(
            draw_list.vertices()[1].position,
            Math::Vec3(box.maximum.x, box.minimum.y, box.minimum.z));
        EXPECT_EQ(
            draw_list.vertices()[22].position,
            Math::Vec3(box.minimum.x, box.maximum.y, box.minimum.z));
        EXPECT_EQ(
            draw_list.vertices()[23].position,
            Math::Vec3(box.minimum.x, box.maximum.y, box.maximum.z));
    }

    TEST(DebugDrawListTest, RejectsInvalidInputWithoutPartialAppend) {
        DebugDrawList draw_list;
        const float nan = std::numeric_limits<float>::quiet_NaN();

        EXPECT_FALSE(draw_list.add_line(
            Math::Vec3(nan, 0.0f, 0.0f),
            Math::Vec3(1.0f),
            Math::Vec4(1.0f)));
        EXPECT_FALSE(draw_list.add_box(
            {
                .minimum = Math::Vec3(1.0f),
                .maximum = Math::Vec3(-1.0f)
            },
            Math::Vec4(1.0f)));
        EXPECT_TRUE(draw_list.empty());
    }

    TEST(DebugDrawListTest, ClearPreservesReusableListContract) {
        DebugDrawList draw_list;
        draw_list.reserve_lines(64);
        ASSERT_TRUE(draw_list.add_line(
            Math::Vec3(0.0f), Math::Vec3(1.0f)));

        draw_list.clear();

        EXPECT_TRUE(draw_list.empty());
        EXPECT_EQ(draw_list.line_count(), 0u);
        EXPECT_TRUE(draw_list.add_line(
            Math::Vec3(2.0f), Math::Vec3(3.0f)));
    }
}
