#include <gtest/gtest.h>

#include "render/scene/render_geometry.h"
#include "render/resource/mesh.h"
#include "render/resource/render_resources.h"
#include "support/engine_fixture.h"

#include <limits>

namespace Comet::Tests {
    using RenderGeometryTest = EngineTest;

    TEST_F(RenderGeometryTest, PreparesSharedWorldBoundsAndIgnoresInvalidGeometry) {
        auto uploaded = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(uploaded);
        const auto mesh = uploaded.value();
        std::vector<ResolvedRenderItem> items{
            {.model_matrix = Math::compose_trs({3, 0, 0}, {}, {-2, 1, 1}), .mesh = mesh},
            {.model_matrix = Math::translate(Math::Mat4(1), {-3, 0, 0}), .mesh = mesh},
            {.mesh = mesh}, {}};
        items[2].model_matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
        RenderGeometry geometry;
        geometry.prepare(items);
        ASSERT_EQ(geometry.get_items().size(), items.size());
        EXPECT_EQ(geometry.get_items()[0].source, &items[0]);
        ASSERT_TRUE(geometry.get_items()[0].world_bounds);
        EXPECT_EQ(geometry.get_items()[0].world_bounds->minimum, Math::Vec3(1, -1, 0));
        EXPECT_EQ(geometry.get_items()[0].world_bounds->maximum, Math::Vec3(5, 1, 1));
        EXPECT_FALSE(geometry.get_items()[2].world_bounds);
        EXPECT_FALSE(geometry.get_items()[3].world_bounds);
        ASSERT_TRUE(geometry.get_scene_bounds());
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(-4, -1, 0));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(5, 1, 1));
    }

    TEST_F(RenderGeometryTest, RefreshesMovementMeshReplacementAndRemoval) {
        auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        std::vector<ResolvedRenderItem> items{{.mesh = mesh.value()}};
        RenderGeometry geometry;
        geometry.prepare(items);
        ASSERT_TRUE(geometry.get_scene_bounds());
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(1));

        items[0].model_matrix = Math::translate(Math::Mat4(1), {5, 0, 0});
        geometry.prepare(items);
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(5, 0, 0));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(6, 1, 1));
        auto replacement = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-2, -2, -2}}, {{2, -2, -2}}, {{0, 2, 2}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(replacement);
        items[0].mesh = replacement.value();
        geometry.prepare(items);
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(3, -2, -2));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(7, 2, 2));

        geometry.clear();
        EXPECT_TRUE(geometry.get_items().empty());
        EXPECT_FALSE(geometry.get_scene_bounds());
        items.clear();
        geometry.prepare(items);
        EXPECT_TRUE(geometry.get_items().empty());
        EXPECT_FALSE(geometry.get_scene_bounds());
    }
}
