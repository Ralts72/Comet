#include "render/scene/render_geometry.h"
#include "render/material/material.h"
#include "render/material/view_lighting.h"
#include "render/passes/shadow_pass.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(ViewWorkSelectionTest, FixedDepthCullingPrecedesLodWithoutChangingMainViewCandidates) {
        auto lod = std::make_shared<ResolvedMeshLod>();
        lod->settings.level1 = AssetHandle(2);
        auto builtin = std::make_shared<Material>("builtin", "pbr");
        auto custom = std::make_shared<Material>("custom", "pbr", AssetHandle(3));
        RenderWorld world{.scene_lifetime = 1};
        world.render_items = {{.entity_id = 1, .material = {.resource = builtin}, .lod = lod},
            {.entity_id = 2, .material = {.resource = builtin}, .lod = lod},
            {.entity_id = 3, .material = {.resource = custom}, .lod = lod}};
        RenderGeometry geometry, shadow, other_view;
        geometry.prepare(world);
        const BoundingBox visible{{-.1f, -.1f, .2f}, {.1f, .1f, .4f}};
        const BoundingBox hidden{{10, 10, .2f}, {11, 11, .4f}};
        geometry.replace_skinned(0, world.render_items[0], visible);
        geometry.replace_skinned(1, world.render_items[1], hidden);
        geometry.replace_skinned(2, world.render_items[2], hidden);
        const auto& selected = shadow.prepare_view(geometry, Math::Mat4(1), true);
        ASSERT_EQ(selected.get_items().size(), 1u);
        EXPECT_EQ(selected.get_items()[0].source->entity_id, 1u);
        EXPECT_EQ(shadow.get_view_statistics().culled_items, 2u);
        EXPECT_EQ(shadow.get_view_statistics().lod_selections, 1u);
        // Main 默认不提前裁剪，自定义顶点程序的可见性由实际材质管线决定。
        EXPECT_EQ(geometry.get_items().size(), 3u);
        EXPECT_EQ(other_view.prepare_view(geometry, Math::Mat4(1)).get_items().size(), 3u);
        EXPECT_EQ(other_view.get_view_statistics().lod_selections, 3u);
    }

    TEST(ViewWorkSelectionTest, UnavailableViewLightingResourcesRemainUnselected) {
        RenderWorld world;
        world.environment.lighting = true;
        world.reflection_probes.resize(LightingData::MAX_REFLECTION_PROBES + 2);
        world.irradiance_volumes.resize(2);
        const std::array input{RenderLight{.intensity = 2}};
        const auto lights = LightingData::prepare(input);
        const auto selected = ViewLightingData::prepare(
            {world, ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)}}, lights);
        EXPECT_FALSE(selected.environment);
        EXPECT_EQ(selected.irradiance_volume_count, 0u);
        EXPECT_EQ(selected.probe_count, 0u);
        EXPECT_EQ(selected.excess_probes, 0u);
        EXPECT_EQ(selected.excess_irradiance_volumes, 0u);
        EXPECT_EQ(selected.lighting.light_count, lights.light_count);
        EXPECT_EQ(selected.lighting.lights[0].intensity, 2);
    }

    TEST(ViewWorkSelectionTest, ShadowDemandRequiresAPreparedShadowView) {
        const std::array lights{RenderLight{.casts_shadow = true}};
        auto lighting = LightingData::prepare(lights);
        EXPECT_FALSE(ShadowPass::needs_shadow(lighting));
        lighting.shadows.back().light_index = 0;
        EXPECT_TRUE(ShadowPass::needs_shadow(lighting));
        lighting.shadows.back().light_index = -1;
        EXPECT_FALSE(ShadowPass::needs_shadow(lighting));
    }
}
