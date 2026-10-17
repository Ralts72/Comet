#include "assets/terrain_editing.h"
#include "assets/editor_assets.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "scene/scene.h"
#include "scene/prefab.h"
#include "support/temporary_directory.h"
#include "support/render_resource_factory.h"
#include <gtest/gtest.h>

namespace CometEditor::Tests {
    namespace {
        Comet::TerrainData ground() {
            Comet::TerrainData data;
            data.resolution = 9;
            data.width = data.depth = 8;
            data.height_scale = 4;
            data.chunk_cells = 4;
            data.albedo_resolution = 16;
            data.vegetation.count = 3;
            data.heights.assign(81, .25f);
            return data;
        }
    }
    TEST(TerrainEditingTest, BrushesRespectRadiusClampsAndSmoothFromOneInputSnapshot) {
        auto data = ground();
        TerrainBrush brush{.radius = 2, .strength = 2};
        ASSERT_TRUE(apply_terrain_brush(data, {0, 0}, brush, .1, 1));
        EXPECT_NEAR(data.heights[40], .3f, 1e-6f);
        EXPECT_FLOAT_EQ(data.heights[0], .25f);
        EXPECT_FLOAT_EQ(data.heights[38], .25f);
        brush.mode = TerrainBrush::Mode::Lower;
        ASSERT_TRUE(apply_terrain_brush(data, {0, 0}, brush, .1, 1));
        EXPECT_NEAR(data.heights[40], .25f, 1e-6f);
        data.heights[40] = 1;
        brush.mode = TerrainBrush::Mode::Smooth;
        ASSERT_TRUE(apply_terrain_brush(data, {0, 0}, brush, .1, 1));
        EXPECT_LT(data.heights[40], 1);
        EXPECT_GT(data.heights[39], .25f);
        EXPECT_NEAR(data.heights[39], data.heights[41], 1e-6f);
        EXPECT_NEAR(data.heights[31], data.heights[49], 1e-6f);
        brush.mode = TerrainBrush::Mode::Flatten;
        ASSERT_TRUE(apply_terrain_brush(data, {0, 0}, brush, 10, 2));
        EXPECT_NEAR(data.heights[40], .5f, .001f);
        brush.mode = TerrainBrush::Mode::Raise;
        ASSERT_TRUE(apply_terrain_brush(data, {0, 0}, brush, 10, 0));
        EXPECT_FLOAT_EQ(data.heights[40], 1);
    }
    TEST(TerrainEditingTest, PaintNormalizesWeightsAndLeavesHeightsAndOutsideSamplesUnchanged) {
        auto data = ground();
        const auto heights = data.heights;
        TerrainBrush brush{
            .mode = TerrainBrush::Mode::Paint, .radius = 2, .strength = 10, .layer = 1};
        ASSERT_TRUE(apply_terrain_brush(data, {0, 0}, brush, .1, 0));
        ASSERT_EQ(data.layer_weights.size(), data.heights.size());
        EXPECT_EQ(data.heights, heights);
        EXPECT_GT(data.layer_weights[40].y, .6f);
        EXPECT_EQ(data.layer_weights[0], Comet::Math::Vec3(1, 0, 0));
        ASSERT_TRUE(data.validate());
        auto encoded = data.serialize();
        ASSERT_TRUE(encoded);
        auto decoded = Comet::TerrainData::deserialize(encoded.value());
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded.value(), data);
        data.layer_weights[0] = {1, 1, 0};
        EXPECT_FALSE(data.validate());
    }
    TEST(TerrainEditingTest, PickingTraversesCellsAndMatchesTriangleHeights) {
        auto data = ground();
        data.heights[40] = .75f;
        auto center = intersect_terrain(data, {{0, 8, 0}, {0, -1, 0}, 10});
        ASSERT_TRUE(center);
        EXPECT_NEAR(center->y, 3, 1e-5f);
        auto diagonal = intersect_terrain(data, {{-8, 4, 0}, {1, -.3f, 0}, 30});
        ASSERT_TRUE(diagonal);
        EXPECT_GT(diagonal->x, -4);
        EXPECT_LT(diagonal->x, 4);
        EXPECT_FALSE(intersect_terrain(data, {{8, 8, 0}, {0, -1, 0}, 10}));
        EXPECT_FALSE(intersect_terrain(data, {{0, 8, 0}, {0, -1, 0}, 1}));
    }
    TEST(TerrainEditingTest, StrokePersistsAndRefreshesSharedInstancesWithUndoAndCancel) {
        using namespace Comet;
        Comet::Tests::TemporaryDirectory temporary;
        ProjectPaths paths(temporary.path());
        std::filesystem::create_directories(paths.assets());
        auto original = ground();
        ASSERT_TRUE(original.save(paths.assets() / "ground.terrain"));
        AssetRegistry registry;
        Comet::Tests::FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        EditorAssets assets(paths, registry, factory, scheduler);
        ASSERT_TRUE(assets.refresh().succeeded());
        const auto asset = assets.database().find("ground.terrain")->handle;
        auto model = assets.rebuild_terrain(asset);
        ASSERT_TRUE(model) << model.error().message;
        const auto components = create_scene_component_registry();
        auto prefab = Prefab::from_model(*model.value(), asset, components, "Terrain");
        ASSERT_TRUE(prefab);
        Scene scene;
        auto first = prefab.value().instantiate(
            scene, components, Math::Vec3(0), {}, asset, AssetType::Model);
        auto second = prefab.value().instantiate(
            scene, components, Math::Vec3(20, 0, 0), {}, asset, AssetType::Model);
        ASSERT_TRUE(first && second);
        const auto first_id = first.value().get_uuid(), second_id = second.value().get_uuid();
        const auto count = scene.entity_count();
        CommandHistory history;
        history.bind_scene(&scene);
        TerrainEditing tool(assets, history, components);
        ASSERT_TRUE(tool.select(first.value()));
        const Ray ray{{0, 8, 0}, {0, -1, 0}, 10};
        tool.update(ray, true, true, false, .1);
        ASSERT_TRUE(tool.active());
        tool.cancel();
        EXPECT_FALSE(history.can_undo());
        EXPECT_EQ(TerrainData::load(paths.assets() / "ground.terrain").value(), original);
        tool.brush.mode = TerrainBrush::Mode::Paint;
        tool.brush.layer = 2;
        tool.update(ray, true, true, false, .1);
        tool.update(ray, false, false, true, 0);
        EXPECT_TRUE(tool.error().empty()) << tool.error();
        ASSERT_TRUE(history.can_undo());
        const auto saved = TerrainData::load(paths.assets() / "ground.terrain");
        ASSERT_TRUE(saved);
        EXPECT_EQ(saved.value().heights, original.heights);
        ASSERT_EQ(saved.value().layer_weights.size(), original.heights.size());
        EXPECT_GT(saved.value().layer_weights[40].z, 0);
        EXPECT_EQ(scene.entity_count(), count);
        EXPECT_TRUE(scene.find_entity(first_id));
        EXPECT_EQ(scene.get_world_matrix(scene.find_entity(second_id))[3].x, 20);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(TerrainData::load(paths.assets() / "ground.terrain").value(), original);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(TerrainData::load(paths.assets() / "ground.terrain").value(), saved.value());
        ASSERT_TRUE(tool.select(scene.find_entity(first_id)));
        tool.update(ray, true, true, false, .1);
        tool.cancel();
        EXPECT_EQ(TerrainData::load(paths.assets() / "ground.terrain").value(), saved.value());
    }
}
