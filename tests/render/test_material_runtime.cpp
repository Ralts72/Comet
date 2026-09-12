#include "core/engine.h"
#include "core/window.h"
#include "support/engine_fixture.h"
#include "render/renderer.h"
#include "render/scene/scene_renderer.h"
#include "render/render_context.h"
#include "render/resource/resource_manager.h"
#include "render/resource/mesh_data.h"
#include "render/resource/texture_data.h"
#include "asset/registry.h"
#include "render/material.h"
#include "render/material_runtime.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "render/scene/scene_resolver.h"

#include <array>
#include <cstring>
#include <gtest/gtest.h>

namespace Comet::Tests {

    TEST(MaterialRuntimeTest, PacksDefaultsAndParametersWithoutChangingOldSnapshots) {
        MaterialRuntimeCache cache;
        const auto layout = std::make_shared<MaterialLayout>("solid",
            std::vector<MaterialLayout::TextureProperty>{}, 32,
            std::vector<MaterialLayout::ScalarProperty>{{"intensity", 16, 1.0f}},
            std::vector<MaterialLayout::VectorProperty>{{"color", 0, {1, 1, 1, 1}}});
        const auto material = std::make_shared<Material>("solid", "solid");
        const auto original = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(original);
        ASSERT_EQ(original->parameters.size(), 32u);
        std::array<float, 8> values;
        std::memcpy(values.data(), original->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 1);
        EXPECT_FLOAT_EQ(values[4], 1);
        EXPECT_FLOAT_EQ(values[7], 0);
        material->set_scalar_property("intensity", 0.25f);
        const Math::Vec4 color(0.2f, 0.4f, 0.6f, 0.8f);
        material->set_vector_property("color", color);
        const auto updated = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(updated);
        std::memcpy(values.data(), updated->parameters.data(), sizeof(values));
        for(int component = 0; component < 4; ++component)
            EXPECT_FLOAT_EQ(values[component], color[component]);
        EXPECT_FLOAT_EQ(values[4], 0.25f);
        EXPECT_FLOAT_EQ(values[5], 0);
        EXPECT_FLOAT_EQ(values[6], 0);
        EXPECT_FLOAT_EQ(values[7], 0);
        EXPECT_EQ(updated, cache.prepare(AssetHandle(1), material, layout));
        std::memcpy(values.data(), original->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[1], 1);
        EXPECT_FLOAT_EQ(values[4], 1);
    }

    TEST(MaterialRuntimeTest, ReusesSnapshotAndInvalidatesMaterialOrLayoutIdentity) {
        MaterialRuntimeCache cache;
        const AssetHandle handle(71);
        auto material = std::make_shared<Material>("solid", "solid");
        auto layout = std::make_shared<MaterialLayout>(
            "solid", std::vector<MaterialLayout::TextureProperty>{});
        const auto first = cache.prepare(handle, material, layout);
        ASSERT_TRUE(first);
        EXPECT_TRUE(first->textures.empty());
        cache.collect_unused();
        EXPECT_EQ(first, cache.prepare(handle, material, layout));

        material->set_texture_property("unused", nullptr);
        const auto changed = cache.prepare(handle, material, layout);
        EXPECT_NE(first, changed);
        const auto revision = material->get_revision();
        material->set_texture_property("unused", nullptr);
        EXPECT_EQ(material->get_revision(), revision);
        EXPECT_EQ(changed, cache.prepare(handle, material, layout));

        material = std::make_shared<Material>("replacement", "solid");
        const auto replaced = cache.prepare(handle, material, layout);
        EXPECT_NE(changed, replaced);
        material = std::make_shared<Material>("same revision", "solid");
        const auto same_revision = cache.prepare(handle, material, layout);
        EXPECT_NE(replaced, same_revision);
        layout = std::make_shared<MaterialLayout>(
            "solid", std::vector<MaterialLayout::TextureProperty>{});
        const auto new_layout = cache.prepare(handle, material, layout);
        EXPECT_NE(same_revision, new_layout);
        EXPECT_EQ(new_layout, cache.prepare(handle, material, layout));
    }

    TEST(MaterialRuntimeTest, EvictsUnusedEntriesWithoutInvalidatingExternalSnapshots) {
        MaterialRuntimeCache cache;
        const auto material = std::make_shared<Material>("solid", "solid");
        const auto layout = std::make_shared<MaterialLayout>(
            "solid", std::vector<MaterialLayout::TextureProperty>{});
        const auto snapshot = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(snapshot);
        cache.collect_unused();
        cache.collect_unused();
        EXPECT_NE(snapshot, cache.prepare(AssetHandle(1), material, layout));
        EXPECT_EQ(snapshot->layout, layout);
    }

    TEST(MaterialRuntimeTest, RejectsMissingResourcesAndRecoversAfterLayoutReplacement) {
        MaterialRuntimeCache cache;
        const auto material = std::make_shared<Material>("test", "solid");
        const auto wrong = std::make_shared<MaterialLayout>(
            "other", std::vector<MaterialLayout::TextureProperty>{});
        const auto missing = std::make_shared<MaterialLayout>(
            "solid", std::vector<MaterialLayout::TextureProperty>{{"albedo", 4}});
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, wrong));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), nullptr, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, nullptr));
        const auto fixed = std::make_shared<MaterialLayout>(
            "solid", std::vector<MaterialLayout::TextureProperty>{});
        EXPECT_TRUE(cache.prepare(AssetHandle(1), material, fixed));
    }

    class MaterialRuntimeGpuTest: public EngineTest {
    protected:
        std::shared_ptr<Texture> texture() {
            return engine->get_resource_manager()
                .try_create_texture(
                    {.width = 1, .height = 1, .pixels = {255, 255, 255, 255}})
                .value();
        }
    };

    TEST_F(MaterialRuntimeGpuTest, RendersAcrossSlotsAfterMutationAndAssetReplacement) {
        const MeshData data{
            .vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
            .indices = {0, 1, 2}};
        auto mesh = engine->get_resource_manager().try_create_mesh(data);
        ASSERT_TRUE(mesh);
        auto first = texture();
        const std::weak_ptr<Texture> retired_texture = first;
        const auto second = texture();
        auto material = std::make_shared<Material>("test", "unlit_texture_blend");
        material->set_texture_property("u_Texture0", first);
        material->set_texture_property("u_Texture1", first);
        auto& registry = engine->get_asset_registry();
        ASSERT_TRUE(registry.register_asset(AssetHandle(11), mesh.value()));
        ASSERT_TRUE(registry.register_asset(AssetHandle(12), material));
        const auto solid = std::make_shared<Material>("solid", "unlit_color");
        solid->set_vector_property("color", {0.25f, 0.75f, 0.5f, 1});
        solid->set_scalar_property("intensity", 0.5f);
        ASSERT_TRUE(registry.register_asset(AssetHandle(13), solid));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back({.entity_id = 7,
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)});
        auto& renderer = engine->get_renderer();
        scene.render_items.push_back({.entity_id = 8,
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(13)});
        scene.render_items.push_back(scene.render_items.front());
        int frames = 0;
        for(int attempt = 0; attempt < 20 && frames < 8; ++attempt) {
            engine->get_window().poll_events();
            if(!renderer.prepare_frame()) {
                continue;
            }
            if(frames == 2) {
                material->set_texture_property("u_Texture0", second);
            }
            if(frames == 4) {
                material =
                    std::make_shared<Material>("replacement", "unlit_texture_blend");
                material->set_texture_property("u_Texture0", first);
                material->set_texture_property("u_Texture1", second);
                EXPECT_TRUE(registry.replace_asset(AssetHandle(12), material));
            }
            if(frames == 3)
                solid->set_scalar_property("intensity", 0.75f);
            if(frames == 6)
                solid->set_scalar_property("intensity", 0.75f);
            if(frames == 7) {
                material = std::make_shared<Material>("replacement", "unlit_color");
                EXPECT_TRUE(registry.replace_asset(AssetHandle(12), material));
                first.reset();
            }
            renderer.render_frame(scene);
            const auto& stats = renderer.get_scene_renderer().get_material_statistics();
            EXPECT_EQ(stats.frame_set_count, 2u);
            EXPECT_EQ(stats.draw_calls, 3u);
            EXPECT_EQ(stats.material_binds, 2u);
            EXPECT_EQ(stats.cached_material_versions, 2u);
            EXPECT_EQ(stats.pipeline_binds, frames == 7 ? 1u : 2u);
            uint32_t expected_versions = 0;
            if(frames == 0)
                expected_versions = 2;
            else if(frames == 2 || frames == 3 || frames == 4 || frames == 7)
                expected_versions = 1;
            EXPECT_EQ(stats.material_versions_created, expected_versions);
            ++frames;
        }
        EXPECT_EQ(frames, 8);
        // WSI acquire 可能已经等待并回收旧 slot；这里仅检查最终回收。
        renderer.get_scene_renderer().get_frame_scheduler().wait_for_all_slots();
        EXPECT_TRUE(retired_texture.expired());
        renderer.get_render_context().wait_idle();
    }

    TEST_F(MaterialRuntimeGpuTest, OrdersBindingsAndKeepsOldRevisionTexturesAlive) {
        MaterialRuntimeCache cache;
        const auto material = std::make_shared<Material>("test", "three_textures");
        const auto layout = std::make_shared<MaterialLayout>("three_textures",
            std::vector<MaterialLayout::TextureProperty>{{"c", 7}, {"a", 1}, {"b", 4}});
        const auto first = texture();
        const auto second = texture();
        material->set_texture_property("a", first);
        material->set_texture_property("b", second);
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, layout));
        const auto failure_log = messages.str();
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, layout));
        EXPECT_EQ(messages.str(), failure_log);
        material->set_texture_property("c", first);
        auto old = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(old);
        ASSERT_EQ(old->textures.size(), 3u);
        EXPECT_EQ(old->textures[0].binding, 1u);
        EXPECT_EQ(old->textures[1].binding, 4u);
        EXPECT_EQ(old->textures[2].binding, 7u);
        material->set_texture_property("a", second);
        auto current = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(current);
        EXPECT_TRUE(old->textures.front().texture == first);
        EXPECT_TRUE(current->textures.front().texture == second);
        engine->get_renderer().get_render_context().wait_idle();
    }

    TEST_F(
        MaterialRuntimeGpuTest, ResolverAcceptsAnyMaterialWithoutInspectingProperties) {
        MeshData data;
        data.vertices = {{{-0.5f, 0, -2}}, {{0.5f, 0, -2}}, {{0, 0.5f, -2}}};
        auto result = engine->get_resource_manager().try_create_mesh(data);
        ASSERT_TRUE(result);
        auto mesh = result.value();
        const auto material = std::make_shared<Material>("future", "arbitrary_layout");
        auto& registry = engine->get_asset_registry();
        ASSERT_TRUE(registry.register_asset(AssetHandle(11), mesh));
        ASSERT_TRUE(registry.register_asset(AssetHandle(12), material));
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.render_items.push_back({.entity_id = 7,
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)});
        auto submission = resolver.resolve(scene, {});
        ASSERT_EQ(submission.render_items.size(), 1u);
        EXPECT_EQ(submission.render_items.front().entity_id, 7u);
        EXPECT_TRUE(submission.render_items.front().material.resource == material);
        EXPECT_EQ(
            submission.render_items.front().material.material_handle, AssetHandle(12));
        EXPECT_TRUE(submission.render_items.front().mesh == mesh);
        engine->get_renderer().get_render_context().wait_idle();
    }
}
