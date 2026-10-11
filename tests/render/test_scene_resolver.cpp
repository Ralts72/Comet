#include <gtest/gtest.h>

#include "asset/registry.h"
#include "common/scope_exit.h"
#include "render/material/material.h"
#include "render/resource/render_resources.h"
#include "render/resource/texture.h"
#include "asset/data/texture_data.h"
#include "render/resource/environment.h"
#include "render/resource/mesh.h"
#include "render/scene/scene_resolver.h"
#include "scene/material_parameters.h"
#include "support/engine_fixture.h"
#include "support/math_assertions.h"
#include "support/render_snapshot_assertions.h"
#include "support/render_resource_factory.h"

#include <limits>
#include <algorithm>
#include <array>
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>

namespace Comet::Tests {
    namespace {
        RenderView runtime_view(const Math::Vec2u size) {
            return RenderView{.render_size = size};
        }
    }

    TEST(SceneResolverTest, CameraProjectionRejectsInvalidAspectAndNonfiniteView) {
        RenderCamera camera;
        EXPECT_FALSE(camera.projection_matrix(0));
        EXPECT_FALSE(camera.projection_matrix(-1));
        camera.view_matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(camera.projection_issue(1), RenderCamera::ProjectionIssue::InvalidView);
        EXPECT_FALSE(camera.projection_matrix(1));
    }

    TEST(SceneResolverTest, ActiveEmptySceneStillReportsMissingPrimaryCameraOnce) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderCameraResolver camera_resolver;
        const auto logger = Logger::get_console_logger();
        ASSERT_NE(logger, nullptr);
        std::ostringstream messages;
        const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
        Logger::add_custom_sink(sink);
        const ScopeExit remove_sink([&] { std::erase(logger->sinks(), sink); });

        const RenderWorld world = resolver.resolve(RenderScene{});

        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view({1280, 720})));
        EXPECT_TRUE(world.render_items.empty());
        const auto reported = messages.str();
        EXPECT_NE(reported.find("Render scene has no primary camera"), std::string::npos);
        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view({1280, 720})));
        EXPECT_EQ(messages.str(), reported);
    }

    TEST(SceneResolverTest, CameraDiagnosticsRemainIndependentAcrossViews) {
        RenderWorld world;
        RenderCameraResolver game, scene;
        RenderView scene_view{.render_size = {1280, 720},
            .camera_selection = RenderView::CameraSelection::Override,
            .camera_override = RenderCamera{}};
        const auto logger = Logger::get_console_logger();
        ASSERT_TRUE(logger);
        std::ostringstream messages;
        const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
        Logger::add_custom_sink(sink);
        const ScopeExit remove_sink([&] { std::erase(logger->sinks(), sink); });
        EXPECT_FALSE(game.resolve(world, runtime_view({1280, 720})));
        const auto first = messages.str();
        ASSERT_NE(first.find("no primary camera"), std::string::npos);
        for(int frame = 0; frame < 3; ++frame) {
            EXPECT_TRUE(scene.resolve(world, scene_view));
            EXPECT_FALSE(game.resolve(world, runtime_view({1280, 720})));
        }
        EXPECT_EQ(messages.str(), first);
        world.cameras.push_back({.primary = true});
        EXPECT_TRUE(game.resolve(world, runtime_view({1280, 720})));
        world.cameras.clear();
        EXPECT_FALSE(game.resolve(world, runtime_view({1280, 720})));
        EXPECT_GT(messages.str().size(), first.size());
    }

    TEST(SceneResolverTest, SkipsItemsWithMissingResources) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderScene render_scene;
        render_scene.render_items.push_back({.entity_id = 7,
            .model_matrix = Math::Mat4(1.0f),
            .mesh_handle = AssetHandle(11),
            .material_handle = AssetHandle(12)});

        const RenderWorld world = resolver.resolve(render_scene);

        EXPECT_TRUE(world.render_items.empty());
    }

    TEST(SceneResolverTest, MissingResourceDiagnosticsExpireWhenReferencesLeaveTheScene) {
        AssetRegistry registry;
        FakeRenderResourceFactory resources;
        const auto mesh = resources.try_create_mesh({});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(registry.register_asset(AssetHandle(10), mesh.value()));
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items = {{.mesh_handle = AssetHandle(11), .material_handle = AssetHandle(20)},
            {.mesh_handle = AssetHandle(10), .material_handle = AssetHandle(21)}};
        const auto logger = Logger::get_console_logger();
        ASSERT_TRUE(logger);
        std::ostringstream messages;
        const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
        Logger::add_custom_sink(sink);
        const ScopeExit remove_sink([&] { std::erase(logger->sinks(), sink); });
        EXPECT_TRUE(resolver.resolve(scene).render_items.empty());
        const auto reported = messages.str();
        EXPECT_NE(reported.find("missing mesh handle 11"), std::string::npos);
        EXPECT_NE(reported.find("missing material handle 21"), std::string::npos);
        EXPECT_TRUE(resolver.resolve(scene).render_items.empty());
        EXPECT_EQ(messages.str(), reported);

        const auto items = std::exchange(scene.render_items, {});
        EXPECT_TRUE(resolver.resolve(scene).render_items.empty());
        EXPECT_EQ(messages.str(), reported);
        scene.render_items = items;
        EXPECT_TRUE(resolver.resolve(scene).render_items.empty());
        EXPECT_NE(
            messages.str().find("missing mesh handle 11", reported.size()), std::string::npos);
        EXPECT_NE(
            messages.str().find("missing material handle 21", reported.size()), std::string::npos);
    }

    using SceneEnvironmentResolverTest = EngineTest;

    TEST(SceneResolverTest, StructuralEditsPreserveResourcesTransformsAndFrozenCopies) {
        AssetRegistry registry;
        FakeRenderResourceFactory resources;
        // 交错引用并让版本缓存发生碰撞，结果仍与无缓存的完整解析一致。
        for(const auto handle : {AssetHandle{10}, AssetHandle{266}})
            ASSERT_TRUE(registry.register_asset(handle, resources.try_create_mesh({}).value()));
        for(const auto handle : {AssetHandle{20}, AssetHandle{276}, AssetHandle{532}})
            ASSERT_TRUE(registry.register_asset(handle, std::make_shared<Material>("item", "pbr")));
        const auto make_item = [](EntityId id) {
            auto overrides = std::make_shared<MaterialOverrides>();
            overrides->scalar_properties["roughness"] = float(id) / 100;
            return RenderItem{.entity_id = id,
                .transform_revision = 1,
                .model_matrix = Math::translate(Math::Mat4(1), {float(id), 0, 0}),
                .mesh_handle = AssetHandle{10 + (id % 2) * 256},
                .material_handle = AssetHandle{20 + (id % 3) * 256},
                .material_overrides = std::move(overrides)};
        };
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.scene_lifetime = 1;
        scene.cameras.push_back({.primary = true});
        scene.render_items = {make_item(1), make_item(2), make_item(3), make_item(4)};
        for(EntityId id = 5; id <= 260; ++id)
            scene.render_items.push_back(make_item(id));
        RenderWorld world;
        const auto check = [&] {
            resolver.resolve(scene, world);
            const auto fresh = resolver.resolve(scene);
            ASSERT_NO_FATAL_FAILURE(
                expect_render_items_equal(world.render_items, fresh.render_items));
        };
        check();
        ASSERT_EQ(world.render_items.size(), 260u);
        const auto frozen = world;
        for(const auto index : {std::size_t{0}, std::size_t{2}, scene.render_items.size()}) {
            SCOPED_TRACE(index);
            scene.render_items.insert(scene.render_items.begin() + index, make_item(10 + index));
            check();
            scene.render_items.erase(scene.render_items.begin() + index);
            check();
        }
        scene.render_items.insert(scene.render_items.begin() + 1, make_item(20));
        scene.render_items[1].mesh_handle = AssetHandle{99};
        check();
        scene.render_items[1].mesh_handle = AssetHandle{10};
        check();
        ASSERT_TRUE(registry.unregister_asset(AssetHandle{276}));
        check();
        scene.render_items.erase(scene.render_items.begin());
        check();
        ASSERT_TRUE(registry.register_asset(
            AssetHandle{276}, std::make_shared<Material>("replacement", "unlit_color")));
        check();
        scene.render_items.insert(scene.render_items.begin(), make_item(30));
        std::ranges::reverse(scene.render_items);
        check();
        auto& moved = scene.render_items.back();
        ++moved.transform_revision;
        moved.model_matrix = Math::translate(Math::Mat4(1), {100, 0, 0});
        moved.material_handle = AssetHandle{532};
        moved.material_overrides.reset();
        check();
        ++scene.scene_lifetime;
        moved.model_matrix = Math::Mat4(1);
        check();
        scene.render_items.clear();
        check();
        scene.render_items.push_back(make_item(40));
        check();
        scene.render_items.front().transform_revision = 0;
        check();
        scene.render_items.front().model_matrix = Math::Mat4(1);
        check();
        scene.render_items.clear();
        check();
        EXPECT_EQ(frozen.render_items.front().model_matrix[3], Math::Vec4(1, 0, 0, 1));
        EXPECT_EQ(frozen.render_items.front().material.resource->get_name(), "item");
        EXPECT_FLOAT_EQ(
            frozen.render_items.front().material.overrides->scalar_properties.at("roughness"),
            0.01f);
    }

    TEST(SceneResolverTest, PublicationRefreshReleasesOnlyChangedResourcesBeforeResolution) {
        AssetRegistry registry;
        FakeRenderResourceFactory resources;
        auto mesh = resources.try_create_mesh({}).value();
        const auto other_mesh = resources.try_create_mesh({}).value();
        auto material = std::make_shared<Material>("original", "unlit_color");
        const auto other_material = std::make_shared<Material>("unchanged", "unlit_color");
        const std::weak_ptr original_mesh = mesh;
        const std::weak_ptr original_material = material;
        ASSERT_TRUE(registry.register_asset(AssetHandle{10}, mesh));
        ASSERT_TRUE(registry.register_asset(AssetHandle{11}, other_mesh));
        ASSERT_TRUE(registry.register_asset(AssetHandle{20}, material));
        ASSERT_TRUE(registry.register_asset(AssetHandle{21}, other_material));
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.scene_lifetime = 1;
        scene.cameras.push_back({.primary = true});
        scene.render_items = {{.entity_id = 1,
                                  .transform_revision = 1,
                                  .model_matrix = Math::translate(Math::Mat4(1), {2, 0, 0}),
                                  .mesh_handle = AssetHandle{10},
                                  .material_handle = AssetHandle{20}},
            {.entity_id = 2, .mesh_handle = AssetHandle{11}, .material_handle = AssetHandle{21}},
            {.entity_id = 3, .mesh_handle = AssetHandle{10}, .material_handle = AssetHandle{20}}};
        scene.item_changes.begin_update(true);
        RenderWorld world;
        resolver.resolve(scene, world);
        ASSERT_EQ(world.render_items.size(), 3u);
        const auto* slots = world.render_items.data();
        const auto replacement = std::make_shared<Material>("replacement", "pbr");
        ASSERT_TRUE(registry.replace_asset(AssetHandle{20}, replacement));
        material.reset();
        ASSERT_FALSE(original_material.expired());

        resolver.refresh_assets(world);
        EXPECT_TRUE(original_material.expired());
        ASSERT_EQ(world.render_items.size(), 3u);
        EXPECT_EQ(world.render_items.data(), slots);
        EXPECT_FALSE(world.render_items[0].material.resource);
        EXPECT_FALSE(world.render_items[2].material.resource);
        EXPECT_EQ(world.render_items[0].mesh, mesh);
        EXPECT_EQ(world.render_items[1].mesh, other_mesh);
        EXPECT_EQ(world.render_items[1].material.resource, other_material);
        EXPECT_EQ(world.render_items[0].model_matrix, scene.render_items[0].model_matrix);
        {
            auto pending_copy = world;
            resolver.resolve(scene, world);
            resolver.resolve(scene, pending_copy);
            EXPECT_FALSE(world.item_changes.full_update);
            EXPECT_EQ(world.item_changes.slots, (std::vector<std::size_t>{0, 2}));
            EXPECT_TRUE(world.resolution.pending_asset_slots.empty());
            EXPECT_TRUE(pending_copy.resolution.pending_asset_slots.empty());
            for(const auto* output : {&world, &pending_copy}) {
                EXPECT_EQ(output->render_items[0].material.resource, replacement);
                EXPECT_EQ(output->render_items[2].material.resource, replacement);
            }
        }

        ASSERT_TRUE(registry.unregister_asset(AssetHandle{10}));
        mesh.reset();
        ASSERT_FALSE(original_mesh.expired());
        resolver.refresh_assets(world);
        EXPECT_TRUE(original_mesh.expired());
        EXPECT_FALSE(world.render_items[0].mesh);
        EXPECT_FALSE(world.render_items[2].mesh);
        EXPECT_EQ(world.render_items[0].material.resource, replacement);
        ASSERT_TRUE(registry.register_asset(AssetHandle{10}, other_mesh));
        resolver.resolve(scene, world);
        const auto fresh = resolver.resolve(scene);
        ASSERT_NO_FATAL_FAILURE(expect_render_items_equal(world.render_items, fresh.render_items));
        const auto previous_revision = world.render_items[0].mesh_revision;
        ASSERT_TRUE(registry.replace_asset(AssetHandle{10}, other_mesh));
        resolver.refresh_assets(world);
        EXPECT_FALSE(world.render_items[0].mesh);
        resolver.resolve(scene, world);
        EXPECT_EQ(world.render_items[0].mesh, other_mesh);
        EXPECT_NE(world.render_items[0].mesh_revision, previous_revision);
    }

    TEST(SceneResolverTest, EnvironmentRefreshPreservesSettingsAndIgnoresUnrelatedPublication) {
        AssetRegistry registry;
        auto environment = std::make_shared<Environment>();
        const std::weak_ptr original = environment;
        ASSERT_TRUE(registry.register_asset(AssetHandle{17}, environment));
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.environment = {AssetHandle{17}, true, 2, 90};
        RenderWorld world;
        resolver.resolve(scene, world);
        const auto revision = world.environment_revision;
        ASSERT_TRUE(registry.register_asset(
            AssetHandle{20}, std::make_shared<Material>("unrelated", "unlit_color")));
        resolver.refresh_assets(world);
        EXPECT_EQ(world.environment_resource, environment);
        EXPECT_EQ(world.environment_revision, revision);
        scene.environment.intensity = 4;
        resolver.resolve(scene, world);
        EXPECT_EQ(world.environment_resource, environment);
        EXPECT_EQ(world.environment, scene.environment);

        const auto replacement = std::make_shared<Environment>();
        ASSERT_TRUE(registry.replace_asset(AssetHandle{17}, replacement));
        environment.reset();
        ASSERT_FALSE(original.expired());
        resolver.refresh_assets(world);
        EXPECT_TRUE(original.expired());
        EXPECT_FALSE(world.environment_resource);
        EXPECT_EQ(world.environment, scene.environment);
        resolver.resolve(scene, world);
        EXPECT_EQ(world.environment_resource, replacement);
        EXPECT_NE(world.environment_revision, revision);
        scene.environment.background = false;
        resolver.resolve(scene, world);
        EXPECT_FALSE(world.environment_resource);
        EXPECT_EQ(world.environment_revision, 0u);
    }

    TEST_F(
        SceneEnvironmentResolverTest, UnpublishedEnvironmentResolvesAfterPublicationWithoutError) {
        AssetRegistry registry;
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.cameras.push_back({.entity_id = 1, .primary = true});
        scene.environment = {AssetHandle(17), true, 2, 90};
        const auto before = messages.str();
        RenderWorld world;
        for(int frame = 0; frame < 3; ++frame) {
            resolver.resolve(scene, world);
            EXPECT_FALSE(world.environment_resource);
            EXPECT_EQ(world.environment, scene.environment);
        }
        EXPECT_EQ(messages.str(), before);

        TextureData data{
            .width = 1, .height = 1, .pixels = std::vector<uint8_t>(24), .cubemap = true};
        auto texture = engine->get_render_resources().try_create_texture(data);
        ASSERT_TRUE(texture) << texture.error().message;
        auto environment =
            std::make_shared<Environment>(Environment{.background = texture.value()});
        ASSERT_TRUE(registry.register_asset(scene.environment.asset, environment));
        const auto published = messages.str();
        resolver.resolve(scene, world);
        EXPECT_EQ(world.environment_resource, environment);
        scene.environment.background = false;
        resolver.resolve(scene, world);
        EXPECT_FALSE(world.environment_resource);
        scene.environment.lighting = true;
        resolver.resolve(scene, world);
        EXPECT_EQ(world.environment_resource, environment);
        scene.environment.background = true;
        ASSERT_TRUE(registry.unregister_asset(scene.environment.asset));
        resolver.resolve(scene, world);
        EXPECT_FALSE(world.environment_resource);
        EXPECT_EQ(messages.str(), published);
    }

    TEST_F(SceneEnvironmentResolverTest, ReusedSubmissionSeesReplacementAndDropsMissingItems) {
        AssetRegistry registry;
        SceneResolver resolver(registry);
        RenderCameraResolver camera_resolver;
        auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{0, 0, -2}}, {{1, 0, -2}}, {{0, 1, -2}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(registry.register_asset(AssetHandle{10}, mesh.value()));
        auto material = std::make_shared<Material>("original", "unlit_color");
        const std::weak_ptr original = material;
        ASSERT_TRUE(registry.register_asset(AssetHandle{20}, material));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back(
            {.mesh_handle = AssetHandle{10}, .material_handle = AssetHandle{20}});
        scene.lights.push_back({.intensity = 4});
        RenderWorld world;
        resolver.resolve(scene, world);
        ASSERT_EQ(world.render_items.size(), 1u);
        EXPECT_EQ(world.render_items.front().material.resource, material);

        auto replacement = std::make_shared<Material>("replacement", "pbr");
        ASSERT_TRUE(registry.replace_asset(AssetHandle{20}, replacement));
        material.reset();
        EXPECT_FALSE(original.expired());
        resolver.resolve(scene, world);
        ASSERT_EQ(world.render_items.size(), 1u);
        EXPECT_EQ(world.render_items.front().material.resource, replacement);
        EXPECT_TRUE(original.expired());

        ASSERT_TRUE(registry.unregister_asset(AssetHandle{10}));
        resolver.resolve(scene, world);
        EXPECT_TRUE(world.render_items.empty());
        scene.cameras.clear();
        scene.lights.clear();
        resolver.resolve(scene, world);
        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view({1280, 720})));
        EXPECT_TRUE(world.lights.empty());
    }

    TEST_F(SceneEnvironmentResolverTest,
        SharedResourcesPreserveItemsAcrossSkippedInputsAndReplacement) {
        AssetRegistry registry;
        SceneResolver resolver(registry);
        const auto first = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{0, 0, -2}}, {{1, 0, -2}}, {{0, 1, -2}}}, .indices = {0, 1, 2}});
        const auto second = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{0, 0, -3}}, {{1, 0, -3}}, {{0, 1, -3}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        ASSERT_TRUE(registry.register_asset(AssetHandle{10}, first.value()));
        ASSERT_TRUE(registry.register_asset(AssetHandle{11}, second.value()));
        const auto material = std::make_shared<Material>("shared", "unlit_color");
        ASSERT_TRUE(registry.register_asset(AssetHandle{20}, material));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items = {
            {.entity_id = 1, .mesh_handle = AssetHandle{10}, .material_handle = AssetHandle{20}},
            {.entity_id = 2, .mesh_handle = AssetHandle{11}, .material_handle = AssetHandle{99}},
            {.entity_id = 3,
                .model_matrix = Math::translate(Math::Mat4(1), {2, 0, 0}),
                .mesh_handle = AssetHandle{10},
                .material_handle = AssetHandle{20}},
            {.entity_id = 4, .mesh_handle = AssetHandle{11}, .material_handle = AssetHandle{20}},
            {.entity_id = 5, .mesh_handle = AssetHandle{99}, .material_handle = AssetHandle{20}},
            {.entity_id = 6, .mesh_handle = AssetHandle{11}, .material_handle = AssetHandle{20}}};
        RenderWorld world;
        resolver.resolve(scene, world);
        ASSERT_EQ(world.render_items.size(), 4u);
        EXPECT_EQ(world.render_items[0].entity_id, 1u);
        EXPECT_EQ(world.render_items[1].entity_id, 3u);
        EXPECT_EQ(world.render_items[2].entity_id, 4u);
        EXPECT_EQ(world.render_items[3].entity_id, 6u);
        EXPECT_EQ(world.render_items[0].mesh, first.value());
        EXPECT_EQ(world.render_items[1].mesh, first.value());
        EXPECT_EQ(world.render_items[2].mesh, second.value());
        EXPECT_EQ(world.render_items[3].mesh, second.value());
        EXPECT_EQ(world.render_items[1].model_matrix, scene.render_items[2].model_matrix);
        for(const auto& item : world.render_items)
            EXPECT_EQ(item.material.resource, material);
        const auto frozen = world;

        ASSERT_TRUE(registry.replace_asset(AssetHandle{10}, second.value()));
        ASSERT_TRUE(registry.register_asset(AssetHandle{99}, material));
        resolver.resolve(scene, world);
        ASSERT_EQ(world.render_items.size(), 5u);
        EXPECT_EQ(world.render_items[0].mesh, second.value());
        EXPECT_EQ(world.render_items[1].entity_id, 2u);
        EXPECT_EQ(world.render_items[2].mesh, second.value());
        EXPECT_EQ(frozen.render_items[0].mesh, first.value());
        EXPECT_EQ(frozen.render_items[1].mesh, first.value());

        std::ranges::reverse(scene.render_items);
        resolver.resolve(scene, world);
        ASSERT_EQ(world.render_items.size(), 5u);
        constexpr std::array<EntityId, 5> reversed{6, 4, 3, 2, 1};
        for(std::size_t index = 0; index < reversed.size(); ++index) {
            EXPECT_EQ(world.render_items[index].entity_id, reversed[index]);
            EXPECT_EQ(world.render_items[index].mesh, second.value());
        }
        scene.render_items.clear();
        resolver.resolve(scene, world);
        EXPECT_TRUE(world.render_items.empty());
        EXPECT_EQ(frozen.render_items.size(), 4u);
    }

    TEST_F(SceneEnvironmentResolverTest, IncompatibleEnvironmentStillReportsOnceUntilRemoved) {
        AssetRegistry registry;
        SceneResolver resolver(registry);
        RenderScene scene;
        scene.cameras.push_back({.entity_id = 1, .primary = true});
        scene.environment = {AssetHandle(17), true, 1, 0};
        TextureData data{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}};
        auto texture = engine->get_render_resources().try_create_texture(data);
        ASSERT_TRUE(texture) << texture.error().message;
        ASSERT_TRUE(registry.register_asset(scene.environment.asset, texture.value()));
        const auto before = messages.str().size();
        EXPECT_FALSE(resolver.resolve(scene).environment_resource);
        const auto reported = messages.str();
        EXPECT_NE(reported.find("expected environment resource", before), std::string::npos);
        EXPECT_FALSE(resolver.resolve(scene).environment_resource);
        EXPECT_EQ(messages.str(), reported);

        ASSERT_TRUE(registry.unregister_asset(scene.environment.asset));
        EXPECT_FALSE(resolver.resolve(scene).environment_resource);
        EXPECT_EQ(messages.str(), reported);
        ASSERT_TRUE(registry.register_asset(
            scene.environment.asset, std::make_shared<Material>("wrong type", "unlit_color")));
        EXPECT_FALSE(resolver.resolve(scene).environment_resource);
        EXPECT_NE(messages.str().find("expected environment resource", reported.size()),
            std::string::npos);
    }

    TEST(SceneResolverTest, BuildsViewProjectionFromPrimaryCamera) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderCameraResolver camera_resolver;
        const Math::Mat4 view_matrix =
            Math::translate(Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -3.0f));
        RenderWorld world;
        world.cameras.push_back({.entity_id = 7,
            .primary = true,
            .view_matrix = view_matrix,
            .fov_degrees = 60.0f,
            .near_clip = 0.2f,
            .far_clip = 500.0f});

        const auto matrix = camera_resolver.resolve(world, runtime_view(Math::Vec2u(1600, 900)));

        ASSERT_TRUE(matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(matrix->view, view_matrix));
        EXPECT_TRUE(TestUtils::Mat4Equal(
            matrix->projection, Math::perspective(60.0f, 1600.0f / 900.0f, 0.2f, 500.0f)));
    }

    TEST(SceneResolverTest, CameraOverrideWinsOverScenePrimary) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderCameraResolver camera_resolver;
        RenderWorld world;
        world.cameras.push_back({.entity_id = 7,
            .primary = true,
            .view_matrix = Math::Mat4(1.0f),
            .fov_degrees = 45.0f});
        const Math::Mat4 editor_view = Math::look_at(
            Math::Vec3(4.0f, 3.0f, 2.0f), Math::Vec3(0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        const auto matrix = camera_resolver.resolve(
            world, RenderView{.render_size = Math::Vec2u(1600, 900),
                       .camera_selection = RenderView::CameraSelection::Override,
                       .camera_override = RenderCamera{.view_matrix = editor_view,
                           .fov_degrees = 70.0f,
                           .near_clip = 0.5f,
                           .far_clip = 250.0f}});

        ASSERT_TRUE(matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(matrix->view, editor_view));
        EXPECT_TRUE(TestUtils::Mat4Equal(
            matrix->projection, Math::perspective(70.0f, 1600.0f / 900.0f, 0.5f, 250.0f)));
    }

    TEST(SceneResolverTest, BuildsOrthographicProjectionFromCameraHeight) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderCameraResolver camera_resolver;
        const Math::Mat4 editor_view = Math::look_at(
            Math::Vec3(0.0f, 0.0f, 3.0f), Math::Vec3(0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        const auto matrix = camera_resolver.resolve(
            RenderWorld{}, RenderView{
                               .render_size = Math::Vec2u(1600, 800),
                               .camera_selection = RenderView::CameraSelection::Override,
                               .camera_override =
                                   RenderCamera{
                                       .view_matrix = editor_view,
                                       .projection = RenderCamera::Projection::Orthographic,
                                       .orthographic_height = 10.0f,
                                       .near_clip = 0.1f,
                                       .far_clip = 100.0f,
                                   },
                           });

        ASSERT_TRUE(matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(matrix->view, editor_view));
        EXPECT_TRUE(TestUtils::Mat4Equal(
            matrix->projection, Math::ortho(-10.0f, 10.0f, -5.0f, 5.0f, 0.1f, 100.0f)));
    }

    TEST(SceneResolverTest, MissingCameraOverrideDoesNotFallBackToScene) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderCameraResolver camera_resolver;
        RenderWorld world;
        world.cameras.push_back({.entity_id = 7, .primary = true});

        const auto matrix = camera_resolver.resolve(
            world, RenderView{.render_size = Math::Vec2u(1280, 720),
                       .camera_selection = RenderView::CameraSelection::Override});

        EXPECT_FALSE(matrix);
    }

    TEST(SceneResolverTest, SelectsLowestEntityIdWhenMultipleCamerasArePrimary) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderCameraResolver camera_resolver;
        const Math::Mat4 selected_view =
            Math::translate(Math::Mat4(1.0f), Math::Vec3(0.0f, 0.0f, -5.0f));
        RenderWorld world;
        world.cameras.push_back({.entity_id = 9, .primary = true, .view_matrix = Math::Mat4(1.0f)});
        world.cameras.push_back({.entity_id = 3, .primary = true, .view_matrix = selected_view});

        const auto matrix = camera_resolver.resolve(world, runtime_view(Math::Vec2u(1280, 720)));

        ASSERT_TRUE(matrix);
        EXPECT_TRUE(TestUtils::Mat4Equal(matrix->view, selected_view));
    }

    TEST(SceneResolverTest, RejectsInvalidCameraParametersAndRenderSize) {
        const AssetRegistry asset_registry;
        SceneResolver resolver(asset_registry);
        RenderCameraResolver camera_resolver;
        RenderWorld world;
        world.cameras.push_back({.entity_id = 7, .primary = true, .fov_degrees = 180.0f});

        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view(Math::Vec2u(1280, 720))));

        world.cameras.front().fov_degrees = 45.0f;
        world.cameras.front().near_clip = 1.0f;
        world.cameras.front().far_clip = 0.5f;
        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view(Math::Vec2u(1280, 720))));

        world.cameras.front().far_clip = 100.0f;
        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view(Math::Vec2u(0, 720))));
        EXPECT_TRUE(camera_resolver.resolve(world, runtime_view(Math::Vec2u(1280, 720))));

        world.cameras.front().projection = RenderCamera::Projection::Orthographic;
        world.cameras.front().orthographic_height = 0.0f;
        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view(Math::Vec2u(1280, 720))));
        world.cameras.front().orthographic_height = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(camera_resolver.resolve(world, runtime_view(Math::Vec2u(1280, 720))));
        world.cameras.front().orthographic_height = 10.0f;
        EXPECT_TRUE(camera_resolver.resolve(world, runtime_view(Math::Vec2u(1280, 720))));
    }
}
