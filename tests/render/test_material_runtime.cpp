#include "core/engine.h"
#include "asset/registry.h"
#include "render/material.h"
#include "render/material_runtime.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "render/scene/scene_resolver.h"
#include "diagnostics/logger.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>

namespace Comet::Tests {
    TEST(MaterialRuntimeTest, ValidatesAndOrdersLayoutSlots) {
        const MaterialLayout layout("textured", 2, {{"detail", 7}, {"albedo", 1}});
        EXPECT_EQ(layout.get_revision(), 2u);
        EXPECT_EQ(layout.get_textures().front().name, "albedo");
        EXPECT_THROW(MaterialLayout("", 1, {}), std::invalid_argument);
        EXPECT_THROW(MaterialLayout("test", 0, {}), std::invalid_argument);
        EXPECT_THROW(MaterialLayout("test", 1, {{"", 2}}), std::invalid_argument);
        EXPECT_THROW(
            MaterialLayout("test", 1, {{"a", 2}, {"a", 3}}), std::invalid_argument);
        EXPECT_THROW(
            MaterialLayout("test", 1, {{"a", 2}, {"b", 2}}), std::invalid_argument);
    }

    TEST(MaterialRuntimeTest, ReusesSnapshotAndInvalidatesMaterialOrLayoutIdentity) {
        MaterialRuntimeCache cache;
        const AssetHandle handle(71);
        auto material = std::make_shared<Material>("solid", "solid");
        auto layout = std::make_shared<MaterialLayout>(
            "solid", 1, std::vector<MaterialLayout::TextureProperty>{});
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
        layout = std::make_shared<MaterialLayout>(
            "solid", 2, std::vector<MaterialLayout::TextureProperty>{});
        EXPECT_NE(replaced, cache.prepare(handle, material, layout));
    }

    TEST(MaterialRuntimeTest, EvictsUnusedEntriesWithoutInvalidatingExternalSnapshots) {
        MaterialRuntimeCache cache;
        const auto material = std::make_shared<Material>("solid", "solid");
        const auto layout = std::make_shared<MaterialLayout>(
            "solid", 1, std::vector<MaterialLayout::TextureProperty>{});
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
            "other", 1, std::vector<MaterialLayout::TextureProperty>{});
        const auto missing = std::make_shared<MaterialLayout>(
            "solid", 1, std::vector<MaterialLayout::TextureProperty>{{"albedo", 4}});
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, wrong));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), nullptr, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, nullptr));
        const auto fixed = std::make_shared<MaterialLayout>(
            "solid", 2, std::vector<MaterialLayout::TextureProperty>{});
        EXPECT_TRUE(cache.prepare(AssetHandle(1), material, fixed));
    }

    class MaterialRuntimeGpuTest: public ::testing::Test {
    protected:
        void SetUp() override {
            log_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(log_sink);
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            engine = std::make_unique<Engine>(config);
        }

        void TearDown() override {
            engine->get_renderer().get_render_context().wait_idle();
            engine.reset();
            if(auto logger = Logger::get_console_logger()) {
                std::erase(logger->sinks(), log_sink);
            }
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos)
                << messages.str();
        }

        std::shared_ptr<Texture> texture() {
            return engine->get_resource_manager()
                .try_create_texture(
                    {.width = 1, .height = 1, .pixels = {255, 255, 255, 255}})
                .value();
        }

        std::unique_ptr<Engine> engine;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> log_sink;
    };

    TEST_F(MaterialRuntimeGpuTest, RendersAcrossSlotsAfterMutationAndAssetReplacement) {
        const MeshData data{
            .vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
            .indices = {0, 1, 2}};
        auto mesh = engine->get_resource_manager().try_create_mesh(data);
        ASSERT_TRUE(mesh);
        const auto first = texture();
        const auto second = texture();
        auto material = std::make_shared<Material>("test", "cube_texture");
        material->set_texture_property("u_Texture0", first);
        material->set_texture_property("u_Texture1", first);
        auto& registry = engine->get_asset_registry();
        ASSERT_TRUE(registry.register_asset(AssetHandle(11), mesh.value()));
        ASSERT_TRUE(registry.register_asset(AssetHandle(12), material));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back({.entity_id = 7,
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)});
        auto& renderer = engine->get_renderer();
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
                material = std::make_shared<Material>("replacement", "cube_texture");
                material->set_texture_property("u_Texture0", first);
                material->set_texture_property("u_Texture1", second);
                EXPECT_TRUE(registry.replace_asset(AssetHandle(12), material));
            }
            renderer.render_frame(scene);
            ++frames;
        }
        EXPECT_EQ(frames, 8);
        renderer.get_render_context().wait_idle();
    }

    TEST_F(MaterialRuntimeGpuTest, OrdersBindingsAndKeepsOldRevisionTexturesAlive) {
        MaterialRuntimeCache cache;
        const auto material = std::make_shared<Material>("test", "three_textures");
        const auto layout = std::make_shared<MaterialLayout>("three_textures", 1,
            std::vector<MaterialLayout::TextureProperty>{{"c", 7}, {"a", 1}, {"b", 4}});
        const auto first = texture();
        const auto second = texture();
        material->set_texture_property("a", first);
        material->set_texture_property("b", second);
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, layout));
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
