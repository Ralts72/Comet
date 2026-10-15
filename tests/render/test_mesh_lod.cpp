#include <gtest/gtest.h>
#include "render/scene/render_geometry.h"
#include "render/scene/scene_extractor.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include <limits>
#include <algorithm>
#include "scene/scene.h"

namespace Comet::Tests {
    TEST(MeshLodTest, SelectsAvailableLevelsWithIndependentHysteresisAndForcedPreview) {
        MeshLod settings{.level1 = AssetHandle{1}, .level2 = AssetHandle{2}};
        EXPECT_EQ(RenderGeometry::select_lod(settings, .3f), 0);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .2f), 1);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .05f), 2);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .24f, 0), 0);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .24f, 1), 1);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .28f, 1), 0);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .075f, 1), 1);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .075f, 2), 2);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .1f, 2), 1);
        settings.level1 = {};
        EXPECT_EQ(RenderGeometry::select_lod(settings, .2f), 0);
        EXPECT_EQ(RenderGeometry::select_lod(settings, .05f), 2);
        settings.preview = MeshLod::Preview::Level1;
        EXPECT_EQ(RenderGeometry::select_lod(settings, .01f), 0);
        settings.preview = MeshLod::Preview::Level2;
        EXPECT_EQ(RenderGeometry::select_lod(settings, 10), 2);
        settings.preview = MeshLod::Preview::Original;
        EXPECT_EQ(RenderGeometry::select_lod(settings, .01f), 0);
    }

    TEST(MeshLodTest, ProjectionUsesRelativeHeightAndHandlesOrthographicAndNearPlane) {
        const BoundingBox bounds{{-1, -1, -1}, {1, 1, 1}};
        RenderCamera camera{.view_matrix = Math::look_at({0, 0, 10}, {0, 0, 0}, {0, 1, 0})};
        const auto size = [&](Math::Vec2u resolution) {
            return RenderGeometry::projected_height(bounds,
                *camera.projection_matrix(float(resolution.x) / resolution.y) * camera.view_matrix);
        };
        EXPECT_FLOAT_EQ(size({1280, 720}), size({2560, 1440}));
        EXPECT_FLOAT_EQ(size({1280, 720}), size({720, 1280}));
        const auto near_height = size({1280, 720});
        camera.view_matrix = Math::look_at({0, 0, 30}, {0, 0, 0}, {0, 1, 0});
        EXPECT_LT(size({1280, 720}), near_height / 2);
        camera.projection = RenderCamera::Projection::Orthographic;
        camera.orthographic_height = 20;
        EXPECT_NEAR(size({1280, 720}), .1f, 1e-6f);
        camera.view_matrix = Math::look_at({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
        EXPECT_NEAR(size({1280, 720}), .1f, 1e-6f);
        camera.projection = RenderCamera::Projection::Perspective;
        camera.view_matrix = Math::Mat4(1);
        EXPECT_EQ(size({1280, 720}), std::numeric_limits<float>::infinity());
        MeshLod settings{.level1 = AssetHandle{1}};
        EXPECT_EQ(RenderGeometry::select_lod(settings, size({1280, 720})), 0);
    }

    TEST(MeshLodTest, SerializationReferencesCloneAndIncrementalExtractionShareTheComponent) {
        auto registry = create_scene_component_registry();
        SceneSerializer serializer(registry);
        Scene scene;
        auto entity = scene.create_entity("LOD");
        entity.add_component<MeshRendererComponent>(AssetHandle{10}, AssetHandle{20});
        const MeshLod settings{.level1 = AssetHandle{11}, .level2 = AssetHandle{12}};
        entity.add_component<MeshLodComponent>(settings);
        auto text = serializer.serialize(scene);
        ASSERT_TRUE(text);
        auto loaded = serializer.deserialize(text.value());
        ASSERT_TRUE(loaded) << loaded.error();
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone);
        EXPECT_EQ(clone.value()->find_entity(entity.get_uuid()).get_component<MeshLodComponent>(),
            settings);
        const auto references =
            registry.collect_asset_references(scene, ComponentRegistry::ReferenceScope::Runtime);
        for(auto handle : {settings.level1, settings.level2})
            EXPECT_TRUE(std::ranges::any_of(references, [&](const auto& value) {
                return value.handle == handle && value.type == AssetType::Mesh;
            }));
        SceneExtractor extractor;
        auto first = extractor.update(scene);
        ASSERT_EQ(first->render_items.size(), 1u);
        EXPECT_EQ(first->render_items[0].lod, settings);
        entity.edit_component<MeshLodComponent>([](auto& lod) { lod.level1_height = .5f; });
        auto second = extractor.update(scene);
        ASSERT_TRUE(second->render_items[0].lod);
        EXPECT_EQ(second->render_items[0].lod->level1_height, .5f);
        EXPECT_EQ(first->render_items[0].lod->level1_height, .25f);
        entity.remove_component<MeshLodComponent>();
        EXPECT_FALSE(extractor.update(scene)->render_items[0].lod);
    }
}
