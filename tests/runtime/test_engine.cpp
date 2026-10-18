#include "asset/project_prepare.h"
#include "asset/runtime/prefab_loader.h"
#include "scene/systems/audio_system.h"
#include "scene/systems/physics_system.h"
#include "core/engine.h"
#include "core/task_scheduler.h"
#include "config/config.h"
#include "core/window.h"
#include "render/renderer.h"
#include "graphics/device.h"
#include "render/frame_scheduler.h"
#include "support/engine_fixture.h"
#include "support/temporary_directory.h"
#include "support/scene_motion_system.h"
#include "scene/systems/camera_controller.h"
#include "scene/script_component.h"
#include "asset/script.h"
#include "scene/systems/script_system.h"
#include "asset/registry.h"
#include "asset/asset_manager.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "asset/data/mesh_data.h"
#include "asset/data/environment_data.h"
#include "asset/runtime/render_asset_factory.h"
#include "render/material/material.h"
#include "render/resource/render_resources.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "render/resource/environment.h"
#include "render/scene/scene_renderer.h"

#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <type_traits>
#include <thread>
#include <future>
#include <vector>

namespace Comet::Tests {
    static_assert(
        std::is_same_v<decltype(std::declval<Engine&>().get_scene_runtime()), const SceneRuntime&>);

    using EngineSceneActivationTest = EngineTest;
    using RenderThreadTest = EngineTest;

    static void check_character_demo(Engine* engine, bool exported_products) {
        // 独立项目目录，不能借用本机试玩留下的导入缓存。
        TemporaryDirectory directory;
        const std::filesystem::path sample = COMET_SAMPLE_PROJECT_DIRECTORY;
        std::filesystem::copy_file(sample / "project.json", directory.path() / "project.json");
        std::filesystem::copy(sample / "assets", directory.path() / "assets",
            std::filesystem::copy_options::recursive);
        auto project = Project::load(directory.path());
        ASSERT_TRUE(project) << project.error();
        AssetDatabase source_database(project.value().paths());
        ASSERT_TRUE(source_database.scan().succeeded());
        const auto* collider_mesh = source_database.find("meshes/cube.gltf");
        ASSERT_NE(collider_mesh, nullptr);
        const auto collider_handle = collider_mesh->handle;
        TemporaryDirectory products;
        if(exported_products) {
            const auto exported = export_project_assets(project.value(), products.path() / "built");
            ASSERT_TRUE(exported) << exported.error();
            std::filesystem::rename(products.path() / "built", products.path() / "moved");
            std::filesystem::remove_all(directory.path());
            project = Project::load(products.path() / "moved");
            ASSERT_TRUE(project) << project.error();
        } else {
            const auto imported = prepare_project(project.value());
            ASSERT_TRUE(imported) << imported.error();
        }
        const auto components = create_scene_component_registry();
        AssetDatabase level_database(project.value().paths());
        ASSERT_TRUE(level_database.scan().succeeded());
        auto loaded =
            SceneSerializer(components, project_scene_sources(level_database, components))
                .load(
                    (project.value().paths().assets() / project.value().startup_scene()).string());
        ASSERT_TRUE(loaded) << loaded.error();
        auto scene = std::move(loaded).value();
        const auto expected_items = scene->component_count<MeshRendererComponent>();
        AssetManager assets(project.value().paths(), engine->get_asset_registry(),
            engine->get_render_resource_factory(), engine->get_task_scheduler());
        ASSERT_TRUE(assets.scan().snapshot_updated);
        EXPECT_EQ(assets.get_database().is_read_only(), exported_products);
        if(!exported_products)
            ASSERT_TRUE(assets.import_mesh(collider_handle));
        auto collision_only = scene->create_entity("Imported static collision");
        collision_only.edit_transform([](auto& transform) { transform.translation.x = 50; });
        collision_only.add_component<RigidBodyComponent>(
            RigidBodyComponent{.motion = BodyMotion::Static});
        collision_only.add_component<ColliderComponent>(
            ColliderComponent{.shape = ColliderShape::TriangleMesh, .mesh = collider_handle});
        auto collected = assets.collect_scene_references(*scene, components);
        ASSERT_TRUE(collected) << collected.error().message;
        const auto references = std::move(collected).value();
        const auto expected_bodies = scene->component_count<RigidBodyComponent>();
        const auto prepared =
            assets.prepare_references(references, AssetManager::MissingAssetPolicy::FailRequired);
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_TRUE(engine->set_input_actions(project.value().input_actions()));
        ASSERT_TRUE(engine->add_default_scene_systems(
            {.read = [&](AssetHandle handle) { return assets.read_mesh_data(handle); },
                .revision =
                    [&](AssetHandle handle) { return assets.mesh_data_revision(handle); }}));
        bool activated = false;
        unsigned frames = 0;
        unsigned active_frames = 0;
        const auto result = engine->run({.frame_ready = [&](const Engine::FrameContext&) {
            const auto published = assets.process_completions();
            if(!published)
                return Result<void, Error>::failure(published.error());
            if(!activated) {
                const auto ready = assets.references_ready(
                    references, AssetManager::MissingAssetPolicy::FailRequired);
                if(!ready)
                    return Result<void, Error>::failure(ready.error());
                if(ready.value()) {
                    engine->set_scene(std::move(scene));
                    if(auto started = engine->start_scene_runtime(); !started)
                        return started;
                    activated = true;
                }
            } else if(++active_frames == 4) {
                const auto* physics = engine->get_scene_runtime().find_system<PhysicsSystem>();
                EXPECT_NE(physics, nullptr);
                if(physics) {
                    EXPECT_EQ(physics->get_statistics().characters, 1u);
                    EXPECT_EQ(physics->get_statistics().bodies, expected_bodies + 1);
                }
                const auto& statistics =
                    engine->get_renderer().get_scene_renderer().get_material_statistics();
                // Lua 生成的终点门包含门板和三段门框；物理与渲染均应接纳新子树。
                EXPECT_EQ(statistics.render_items, expected_items + 4);
                EXPECT_GT(statistics.draw_calls, 0u);
                engine->get_window().request_close();
            }
            if(++frames == 300)
                return Result<void, Error>::failure({"Character demo activation timed out"});
            return Result<void, Error>::success();
        }});
        ASSERT_TRUE(result) << result.error();
        EXPECT_TRUE(activated);
        EXPECT_EQ(active_frames, 4u);
    }

    TEST_F(EngineSceneActivationTest, CharacterDemoLoadsRealAssetsAndRendersWithDefaultSystems) {
        check_character_demo(engine.get(), false);
    }

    TEST_F(EngineSceneActivationTest, ExportedCharacterDemoRendersAfterSourceAndCacheAreRemoved) {
        check_character_demo(engine.get(), true);
    }

    TEST_F(RenderThreadTest, AssetFactoryRemainsValidBeforeAndAfterRunUntilShutdown) {
        auto& resources = engine->get_render_resource_factory();
        RenderAssetFactory assets(resources);
        auto mesh = assets.prepare(
            MeshData{.vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{0, 1, 0}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh) << mesh.error();
        EXPECT_EQ(mesh.value()->get_local_bounds().maximum, Math::Vec3(1, 1, 0));
        const TextureData pixel{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}};
        auto environment = assets.prepare(EnvironmentData{
            .background = pixel, .irradiance = pixel, .specular = pixel, .brdf = pixel});
        ASSERT_TRUE(environment) << environment.error();
        EXPECT_TRUE(environment.value()->has_lighting());

        unsigned frames = 0;
        const auto result = engine->run({.frame_ready = [&](const Engine::FrameContext&) {
            auto texture = assets.prepare(pixel);
            EXPECT_TRUE(texture) << texture.error();
            if(++frames == 3)
                engine->get_window().request_close();
            return Result<void, Error>::success();
        }});
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(frames, 3u);
        EXPECT_EQ(&engine->get_render_resource_factory(), &resources);
        auto texture = assets.prepare(pixel);
        ASSERT_TRUE(texture) << texture.error();
        EXPECT_EQ(texture.value()->get_width(), 1);
        engine->prepare_shutdown();
        EXPECT_FALSE(assets.prepare(pixel));
    }

    TEST_F(RenderThreadTest, DrawingUsesOneWorkerAndShutdownWaitsForTheLastFrame) {
        const auto main_thread = std::this_thread::get_id();
        std::thread::id render_thread;
        unsigned draws = 0;
        unsigned ready = 0;
        Comet::Tests::set_test_overlay(
            engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
                const auto current = std::this_thread::get_id();
                EXPECT_NE(current, main_thread);
                if(draws == 0)
                    render_thread = current;
                EXPECT_EQ(current, render_thread);
                ++draws;
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine->run({
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    EXPECT_EQ(std::this_thread::get_id(), main_thread);
                    EXPECT_EQ(draws, ready);
                    if(++ready == 3)
                        engine->get_window().request_close();
                    return Result<void, Error>::success();
                },
        });
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(draws, 3u);
        EXPECT_FALSE(engine->get_renderer().get_frame_scheduler().is_frame_active());
        engine->get_renderer().set_overlay({});
    }

    TEST_F(RenderThreadTest, RuntimeUpdateOverlapsDrawingAndPublishesTheNextSceneSnapshot) {
        class MoveOnce final: public System {
        public:
            MoveOnce(Entity object, Renderer& renderer, std::future<void>& drawing,
                std::promise<void>& updated)
                : m_object(object), m_renderer(renderer), m_drawing(drawing), m_updated(updated) {}
            Result<void, Error> update(Scene&, const Context&) override {
                if(++updates != 1)
                    return Result<void, Error>::success();
                const auto started = m_drawing.wait_for(std::chrono::seconds(5));
                const auto rejected = m_renderer.prepare_overlay_resources([] {
                    ADD_FAILURE() << "GPU preparation must not enter an in-flight frame";
                    return Result<void, GraphicsError>::success();
                });
                EXPECT_FALSE(rejected);
                EXPECT_EQ(rejected.error().result, vk::Result::eNotReady);
                const auto edited =
                    m_object.try_edit_transform([](auto& value) { value.translation.x = 1000; });
                m_updated.set_value();
                if(started != std::future_status::ready)
                    return Result<void, Error>::failure({"Drawing did not overlap runtime update"});
                if(!edited)
                    return Result<void, Error>::failure({"Cannot move runtime object"});
                return Result<void, Error>::success();
            }
            unsigned updates = 0;

        private:
            Entity m_object;
            Renderer& m_renderer;
            std::future<void>& m_drawing;
            std::promise<void>& m_updated;
        };
        auto mesh = engine->get_render_resource_factory().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(engine->get_asset_registry().register_asset(AssetHandle{10}, mesh.value()));
        ASSERT_TRUE(engine->get_asset_registry().register_asset(
            AssetHandle{20}, std::make_shared<Material>("item", "unlit_color")));
        auto scene = std::make_unique<Scene>();
        scene->create_entity("Camera").add_component<CameraComponent>(
            CameraComponent{.primary = true});
        auto object = scene->create_entity("Object");
        object.add_component<MeshRendererComponent>(AssetHandle{10}, AssetHandle{20});
        engine->set_scene(std::move(scene));
        std::promise<void> drawing;
        std::promise<void> updated;
        auto drawing_started = drawing.get_future();
        auto update_finished = updated.get_future();
        auto motion =
            std::make_unique<MoveOnce>(object, engine->get_renderer(), drawing_started, updated);
        const auto* observer = motion.get();
        ASSERT_TRUE(engine->add_system(std::move(motion)));
        ASSERT_TRUE(engine->start_scene_runtime());
        unsigned draws = 0;
        const auto main_thread = std::this_thread::get_id();
        set_test_overlay(engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
            EXPECT_NE(std::this_thread::get_id(), main_thread);
            if(++draws == 1) {
                drawing.set_value();
                EXPECT_EQ(
                    update_finished.wait_for(std::chrono::seconds(5)), std::future_status::ready);
            }
            const auto& statistics =
                engine->get_renderer().get_scene_renderer().get_material_statistics();
            EXPECT_EQ(statistics.render_items, 1u);
            EXPECT_EQ(statistics.culled_items, draws == 1 ? 0u : 1u);
            return Result<void, GraphicsError>::success();
        }});
        unsigned frames = 0;
        unsigned captures = 0;
        const auto result = engine->run({.frame_ready =
                                             [&](const Engine::FrameContext&) {
                                                 EXPECT_EQ(observer->updates, frames);
                                                 if(++frames == 2)
                                                     engine->get_window().request_close();
                                                 return Result<void, Error>::success();
                                             },
            .frame_capture =
                [&](const Engine::FrameContext&) {
                    EXPECT_EQ(std::this_thread::get_id(), main_thread);
                    EXPECT_EQ(observer->updates, captures);
                    ++captures;
                }});
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(draws, 2u);
        EXPECT_EQ(observer->updates, 2u);
        EXPECT_EQ(captures, 2u);
        engine->prepare_shutdown();
        engine->get_renderer().set_overlay({});
    }

    TEST_F(RenderThreadTest, LastWorkerErrorSurvivesClosingTheWindow) {
        const GraphicsError failure{"Worker device lost", vk::Result::eErrorDeviceLost};
        const auto main_thread = std::this_thread::get_id();
        unsigned shutdowns = 0;
        Comet::Tests::set_test_overlay(engine->get_renderer(),
            {.render =
                    [&](OverlayRecordContext&) {
                        EXPECT_NE(std::this_thread::get_id(), main_thread);
                        return Result<void, GraphicsError>::failure(failure);
                    },
                .shutdown =
                    [&] {
                        EXPECT_NE(std::this_thread::get_id(), main_thread);
                        ++shutdowns;
                    }});
        const auto result = engine->run({
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    engine->get_window().request_close();
                    return Result<void, Error>::success();
                },
        });
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, failure.as_error().message);
        EXPECT_EQ(result.error().code, failure.as_error().code);
        EXPECT_EQ(shutdowns, 1u);
        EXPECT_FALSE(engine->get_renderer().prepare_frame());
        engine->get_renderer().set_overlay({});
    }

    TEST_F(RenderThreadTest, DrawingFailureTakesPriorityOverConcurrentRuntimeFailure) {
        auto calls = std::make_shared<RuntimeCalls>();
        calls->fail_update = true;
        engine->set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine->add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine->start_scene_runtime());
        const GraphicsError failure{"Worker device lost", vk::Result::eErrorDeviceLost};
        set_test_overlay(engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
            return Result<void, GraphicsError>::failure(failure);
        }});
        unsigned recoveries = 0;
        const auto result = engine->run({.runtime_failed = [&](const Error&) {
            ++recoveries;
            return Result<void, Error>::success();
        }});
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, failure.as_error().message);
        EXPECT_EQ(result.error().code, failure.as_error().code);
        EXPECT_EQ(calls->updates, 1);
        EXPECT_EQ(calls->stops, 1);
        EXPECT_EQ(recoveries, 0u);
        EXPECT_FALSE(engine->get_scene_runtime().is_active());
        EXPECT_FALSE(engine->get_renderer().prepare_frame());
        engine->get_renderer().set_overlay({});
    }

    TEST_F(RenderThreadTest, PausedStepFailureCompletesAcquiredFrameBeforeRecovery) {
        auto calls = std::make_shared<RuntimeCalls>();
        calls->fail_update = true;
        engine->set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine->add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine->start_scene_runtime(SceneRuntime::State::Paused));
        unsigned draws = 0;
        unsigned recoveries = 0;
        unsigned captures = 0;
        set_test_overlay(engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
            ++draws;
            return Result<void, GraphicsError>::success();
        }});
        const auto result = engine->run({
            .frame_ready =
                [&](const Engine::FrameContext&) { return engine->request_runtime_step(); },
            .runtime_failed =
                [&](const Error& error) {
                    EXPECT_EQ(error.message, "runtime update failed");
                    EXPECT_EQ(draws, 1u);
                    EXPECT_FALSE(engine->get_renderer().get_frame_scheduler().is_frame_active());
                    ++recoveries;
                    engine->get_window().request_close();
                    return Result<void, Error>::success();
                },
            .frame_capture = [&](const Engine::FrameContext&) { ++captures; },
        });
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(recoveries, 1u);
        EXPECT_EQ(calls->updates, 1);
        EXPECT_EQ(calls->stops, 1);
        EXPECT_EQ(captures, 0u);
        engine->get_renderer().set_overlay({});
    }

    TEST_F(RenderThreadTest, FrameCaptureObservesCompletedPausedStep) {
        auto calls = std::make_shared<RuntimeCalls>();
        engine->set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine->add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine->start_scene_runtime(SceneRuntime::State::Paused));
        unsigned captures = 0;
        const auto main_thread = std::this_thread::get_id();
        const auto result = engine->run({.frame_ready =
                                             [&](const Engine::FrameContext&) {
                                                 EXPECT_EQ(calls->fixed_updates, 0);
                                                 return engine->request_runtime_step();
                                             },
            .frame_capture =
                [&](const Engine::FrameContext&) {
                    EXPECT_EQ(std::this_thread::get_id(), main_thread);
                    EXPECT_EQ(calls->fixed_updates, 1);
                    EXPECT_EQ(calls->updates, 1);
                    ++captures;
                    engine->get_window().request_close();
                }});
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(captures, 1u);
    }

    TEST_F(RenderThreadTest, SwapchainPreparationAndShutdownUseTheDrawingThread) {
        const auto main_thread = std::this_thread::get_id();
        std::thread::id drawing_thread;
        unsigned rebuilds = 0;
        unsigned releases = 0;
        unsigned shutdowns = 0;
        set_test_overlay(engine->get_renderer(),
            {
                .render =
                    [&](OverlayRecordContext&) {
                        EXPECT_EQ(std::this_thread::get_id(), drawing_thread);
                        return Result<void, GraphicsError>::success();
                    },
                .release =
                    [&] {
                        drawing_thread = std::this_thread::get_id();
                        EXPECT_NE(drawing_thread, main_thread);
                        ++releases;
                    },
                .rebuild =
                    [&](const SwapchainCompatibility&) {
                        EXPECT_EQ(std::this_thread::get_id(), drawing_thread);
                        ++rebuilds;
                        return Result<void, GraphicsError>::success();
                    },
                .shutdown =
                    [&] {
                        EXPECT_EQ(std::this_thread::get_id(), drawing_thread);
                        ++shutdowns;
                    },
            });
        const auto result = engine->run({
            .update =
                [&](const Engine::FrameContext&) {
                    engine->get_renderer().request_swapchain_recreation();
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    const GraphicsError failure{
                        "UI candidate unavailable", vk::Result::eErrorOutOfDeviceMemory};
                    const auto prepared = engine->get_renderer().prepare_overlay_resources([&] {
                        EXPECT_EQ(std::this_thread::get_id(), drawing_thread);
                        EXPECT_TRUE(engine->get_renderer()
                                .get_render_context()
                                .get_device()
                                .is_resource_owner_thread());
                        return Result<void, GraphicsError>::failure(failure);
                    });
                    EXPECT_FALSE(prepared);
                    EXPECT_EQ(prepared.error().message, failure.message);
                    EXPECT_EQ(prepared.error().result, failure.result);
                    engine->get_window().request_close();
                    return Result<void, Error>::success();
                },
        });
        ASSERT_TRUE(result) << result.error();
        EXPECT_EQ(rebuilds, 1u);
        EXPECT_EQ(releases, 1u);
        EXPECT_EQ(shutdowns, 0u);
        engine->prepare_shutdown();
        EXPECT_EQ(shutdowns, 1u);
        engine->prepare_shutdown();
        EXPECT_EQ(shutdowns, 1u);
        engine->get_renderer().set_overlay({});
    }

    TEST_F(RenderThreadTest, PreparationFailurePreservesVulkanErrorAndClosesGpuBackend) {
        const GraphicsError failure{"Cannot rebuild overlay", vk::Result::eErrorOutOfDeviceMemory};
        unsigned shutdowns = 0;
        const auto main_thread = std::this_thread::get_id();
        engine->get_renderer().set_overlay({
            .rebuild =
                [&](const SwapchainCompatibility&) {
                    EXPECT_NE(std::this_thread::get_id(), main_thread);
                    return Result<void, GraphicsError>::failure(failure);
                },
            .shutdown =
                [&] {
                    EXPECT_NE(std::this_thread::get_id(), main_thread);
                    ++shutdowns;
                },
        });
        const auto result = engine->run({.update = [&](const Engine::FrameContext&) {
            engine->get_renderer().request_swapchain_recreation();
            return Result<void, Error>::success();
        }});
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, failure.as_error().message);
        EXPECT_EQ(result.error().code, failure.as_error().code);
        EXPECT_EQ(shutdowns, 1u);
        engine->get_renderer().set_overlay({});
    }

    TEST_F(RenderThreadTest, WorkerVulkanExceptionPreservesItsErrorCategory) {
        Comet::Tests::set_test_overlay(engine->get_renderer(),
            {.render = [](OverlayRecordContext&) -> Result<void, GraphicsError> {
                throw vk::SystemError(
                    vk::make_error_code(vk::Result::eErrorOutOfDeviceMemory), "worker allocation");
            }});
        const auto result = engine->run({.frame_ready = [&](const Engine::FrameContext&) {
            engine->get_window().request_close();
            return Result<void, Error>::success();
        }});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("worker allocation"), std::string::npos);
        EXPECT_EQ(result.error().code,
            (GraphicsError{"", vk::Result::eErrorOutOfDeviceMemory}.as_error().code));
        EXPECT_FALSE(engine->get_renderer().prepare_frame());
        engine->get_renderer().set_overlay({});
    }

    TEST_F(EngineSceneActivationTest, CachedExtractionRefreshesEditsAndReinstalledScene) {
        auto mesh = engine->get_render_resource_factory().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(engine->get_asset_registry().register_asset(AssetHandle{10}, mesh.value()));
        ASSERT_TRUE(engine->get_asset_registry().register_asset(
            AssetHandle{20}, std::make_shared<Material>("item", "unlit_color")));
        auto scene = std::make_unique<Scene>();
        auto camera = scene->create_entity("Camera");
        camera.add_component<CameraComponent>(CameraComponent{.primary = true});
        auto object = scene->create_entity("Object");
        object.add_component<MeshRendererComponent>(AssetHandle{10}, AssetHandle{20});
        engine->set_scene(std::move(scene));
        int frames = 0;
        int updates = 0;
        Comet::Tests::set_test_overlay(
            engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
                const auto& renderer = engine->get_renderer().get_scene_renderer();
                const bool visible =
                    frames == 1 || frames == 2 || frames == 4 || frames == 5 || frames == 8;
                EXPECT_EQ(renderer.get_material_statistics().render_items, visible ? 1u : 0u);
                EXPECT_FLOAT_EQ(renderer.get_post_process_settings().exposure, frames >= 4 ? 2 : 1);
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine->run({.update =
                                             [&](const Engine::FrameContext&) {
                                                 if(++updates > 20)
                                                     engine->get_window().request_close();
                                                 return Result<void, Error>::success();
                                             },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    ++frames;
                    if(frames == 8)
                        engine->get_window().request_close();
                    switch(frames) {
                        case 3:
                            camera.edit_component<CameraComponent>(
                                [](auto& value) { value.fov = 0; });
                            break;
                        case 4:
                            camera.edit_component<CameraComponent>(
                                [](auto& value) { value.fov = 60; });
                            EXPECT_TRUE(engine->get_scene()->set_post_process({.exposure = 2}));
                            break;
                        case 5: {
                            const auto revision = engine->get_scene()->get_render_revision();
                            auto retained = engine->replace_scene(nullptr);
                            engine->set_scene(std::move(retained));
                            EXPECT_EQ(engine->get_scene()->get_render_revision(), revision);
                            break;
                        }
                        case 6:
                            object.edit_component<MeshRendererComponent>(
                                [](auto& value) { value.mesh = AssetHandle{99}; });
                            break;
                        case 7:
                            object.edit_component<MeshRendererComponent>(
                                [](auto& value) { value.mesh = AssetHandle{10}; });
                            camera.remove_component<CameraComponent>();
                            break;
                        case 8:
                            camera.add_component<CameraComponent>(CameraComponent{.primary = true});
                            break;
                        default:
                            break;
                    }
                    return Result<void, Error>::success();
                }});
        engine->get_renderer().set_overlay({});
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(frames, 8);
        EXPECT_NE(messages.str().find("invalid projection/view parameters"), std::string::npos);
        EXPECT_NE(messages.str().find("missing mesh handle 99"), std::string::npos);
        EXPECT_NE(messages.str().find("no primary camera"), std::string::npos);
    }

    TEST_F(EngineSceneActivationTest, FrameRateLimitChangesDuringRunAndWaitsBetweenLoopStarts) {
        ASSERT_TRUE(engine->set_frame_rate_limit(20));
        unsigned updates = 0;
        const auto start = FramePacer::Clock::now();
        const auto result = engine->run({.update = [&](const Engine::FrameContext& frame) {
            ++updates;
            const auto elapsed = FramePacer::Clock::now() - start;
            EXPECT_GT(frame.update.delta_time, 0);
            if(updates == 2) {
                EXPECT_GE(elapsed, std::chrono::milliseconds(45));
                EXPECT_TRUE(engine->set_frame_rate_limit(60));
            }
            if(updates == 3) {
                EXPECT_GE(elapsed, std::chrono::milliseconds(60));
                engine->get_window().request_close();
            }
            return Result<void, Error>::success();
        }});
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(updates, 3u);
        EXPECT_EQ(engine->frame_rate_limit(), 60);
        engine->prepare_shutdown();
        EXPECT_FALSE(engine->set_frame_rate_limit(0));
    }

    TEST_F(EngineSceneActivationTest, CameraIntentLocksWindowAndUiRejectionReleasesIt) {
        auto& window = engine->get_window();
        window.poll_events();
        if(glfwGetWindowAttrib(window.get(), GLFW_FOCUSED) != GLFW_TRUE)
            GTEST_SKIP() << "Cursor locking requires a focused native window";
        auto actions = InputActions::create(
            {{"camera.look", InputActions::Type::Button, {{Input::Key::Space}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(engine->set_input_actions(std::move(actions).value()));
        auto scene = std::make_unique<Scene>();
        auto camera = scene->create_entity();
        camera.add_component<CameraComponent>(CameraComponent{.primary = true});
        camera.add_component<CameraControllerComponent>();
        engine->set_scene(std::move(scene));
        ASSERT_TRUE(engine->add_system(std::make_unique<CameraControllerSystem>()));
        ASSERT_TRUE(engine->start_scene_runtime());
        Input input;
        input.focus_event(true);
        int ready_calls = 0;
        int updates = 0;
        Comet::Tests::set_test_overlay(
            engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine->run({
            .update =
                [&](const Engine::FrameContext&) {
                    EXPECT_EQ(window.is_cursor_locked(), ready_calls == 2);
                    if(++updates > 20)
                        window.request_close();
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    if(++ready_calls == 2)
                        input.key_event(Input::Key::Space, true);
                    if(ready_calls == 3)
                        window.request_close();
                    return Result<void, Error>::success();
                },
            .runtime_input = [&](const Engine::FrameContext&) -> std::optional<Input::Frame> {
                auto routed = input.publish_frame();
                if(ready_calls == 3)
                    routed.pointer_enabled = false;
                return routed;
            },
        });
        engine->get_renderer().set_overlay({});
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(ready_calls, 3);
        EXPECT_FALSE(window.is_cursor_locked());
    }

    TEST_F(EngineSceneActivationTest, LoadingFramesDoNotHideActiveSceneCameraErrors) {
        ASSERT_EQ(engine->get_scene(), nullptr);
        int draws = 0;
        int updates = 0;
        Comet::Tests::set_test_overlay(
            engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;
                const auto warning = messages.str().find("Render scene has no primary camera");
                if(draws <= 2)
                    EXPECT_EQ(warning, std::string::npos);
                else
                    EXPECT_NE(warning, std::string::npos);
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine->run({
            .update =
                [&](const Engine::FrameContext&) {
                    if(++updates > 8)
                        engine->get_window().request_close();
                    if(draws == 2)
                        engine->set_scene(std::make_unique<Scene>());
                    if(draws == 3) {
                        auto camera = engine->get_scene()->create_entity("Camera");
                        camera.add_component<CameraComponent>(CameraComponent{.primary = true});
                    }
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    if(draws == 3)
                        engine->get_window().request_close();
                    return Result<void, Error>::success();
                },
        });
        engine->get_renderer().set_overlay({});
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(draws, 4);
        EXPECT_FALSE(engine->get_renderer().get_frame_scheduler().is_frame_active());
    }

    TEST(EngineRunTest, DefaultSystemsInstallBeforeRuntimeStarts) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        EXPECT_EQ(engine.get_scene_runtime().find_system<ScriptSystem>(), nullptr);
        ASSERT_TRUE(engine.add_default_scene_systems());
        EXPECT_NE(engine.get_scene_runtime().find_system<CameraControllerSystem>(), nullptr);
        EXPECT_NE(engine.get_scene_runtime().find_system<PhysicsSystem>(), nullptr);
        EXPECT_NE(engine.get_scene_runtime().find_system<AudioSystem>(), nullptr);
        const auto* scripts = engine.get_scene_runtime().find_system<ScriptSystem>();
        ASSERT_NE(scripts, nullptr);
        auto scene = std::make_unique<Scene>();
        auto entity = scene->create_entity();
        constexpr AssetHandle handle{42};
        entity.add_component<ScriptComponent>().asset = handle;
        const auto script = Script::create("return {}");
        ASSERT_TRUE(script);
        ASSERT_TRUE(engine.get_asset_registry().register_asset(handle, script.value()));
        engine.set_scene(std::move(scene));
        EXPECT_FALSE(scripts->running_script(entity));
        ASSERT_TRUE(engine.start_scene_runtime());
        EXPECT_EQ(scripts->running_script(entity), script.value());
        EXPECT_FALSE(engine.add_default_scene_systems());
        ASSERT_TRUE(engine.stop_scene_runtime());
        EXPECT_FALSE(scripts->running_script(entity));
        engine.prepare_shutdown();
        EXPECT_EQ(engine.get_scene_runtime().find_system<ScriptSystem>(), nullptr);
        engine.prepare_shutdown();
        EXPECT_EQ(engine.get_scene_runtime().find_system<ScriptSystem>(), nullptr);
    }

    TEST(EngineRunTest, RoutesInputOnceAfterUiWithoutCarryingItIntoTheNextFrame) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        int updates = 0;
        int ready_calls = 0;
        int input_calls = 0;
        int draws = 0;
        const Engine::FrameContext* current_frame = nullptr;
        Comet::Tests::set_test_overlay(
            engine.get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;

                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine.run({
            .update =
                [&](const Engine::FrameContext& frame) {
                    current_frame = &frame;
                    ++updates;
                    if(updates > 1)
                        EXPECT_TRUE(calls->input_focused);
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext& frame) {
                    EXPECT_EQ(&frame, current_frame);
                    if(++ready_calls == 3)
                        engine.get_window().request_close();
                    return Result<void, Error>::success();
                },
            .runtime_input = [&](const Engine::FrameContext& frame) -> std::optional<Input::Frame> {
                EXPECT_EQ(&frame, current_frame);
                EXPECT_EQ(++input_calls, ready_calls);
                EXPECT_EQ(calls->updates, input_calls - 1);
                if(input_calls == 3)
                    return std::nullopt;
                auto routed = frame.physical_input;
                routed.focused = true;
                return routed;
            },
        });
        engine.get_renderer().set_overlay({});
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(updates, 3);
        EXPECT_EQ(ready_calls, 3);
        EXPECT_EQ(input_calls, 3);
        EXPECT_EQ(calls->updates, 3);
        EXPECT_FALSE(calls->input_focused);
    }

    TEST_F(EngineSceneActivationTest, UiCaptureBlocksCurrentPressUntilReleaseAndNewPress) {
        class InputTraceSystem final: public System {
        public:
            std::vector<InputState> frames;
            Result<void, Error> update(Scene&, const Context& context) override {
                frames.push_back(context.input);
                return Result<void, Error>::success();
            }
        };
        auto observer = std::make_unique<InputTraceSystem>();
        const auto& frames = observer->frames;
        ASSERT_TRUE(engine->add_system(std::move(observer)));
        engine->set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine->start_scene_runtime());
        Input physical;
        physical.focus_event(true);
        Input::Gate gate;
        bool ui_blocked = false;
        int updates = 0;
        int inputs = 0;
        int draws = 0;
        Comet::Tests::set_test_overlay(
            engine->get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine->run({
            .update =
                [&](const Engine::FrameContext&) {
                    if(++updates > 10)
                        engine->get_window().request_close();
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    if(draws == 4)
                        engine->get_window().request_close();
                    ui_blocked = draws == 1;
                    if(draws == 1 || draws == 4)
                        physical.key_event(Input::Key::Space, true);
                    if(draws == 3)
                        physical.key_event(Input::Key::Space, false);
                    return Result<void, Error>::success();
                },
            .runtime_input = [&](const Engine::FrameContext&) -> std::optional<Input::Frame> {
                ++inputs;
                return gate.read(physical.publish_frame(), !ui_blocked);
            },
        });
        engine->get_renderer().set_overlay({});
        ASSERT_TRUE(result) << result.error();
        ASSERT_EQ(frames.size(), 5u);
        EXPECT_EQ(inputs, 5);
        EXPECT_TRUE(frames[0].focused());
        EXPECT_FALSE(frames[1].focused());
        EXPECT_TRUE(frames[2].focused());
        for(size_t index = 0; index < 4; ++index) {
            EXPECT_FALSE(frames[index].physical().key(Input::Key::Space).pressed);
            EXPECT_FALSE(frames[index].physical().key(Input::Key::Space).down);
        }
        EXPECT_TRUE(frames[4].physical().key(Input::Key::Space).pressed);
        EXPECT_TRUE(frames[4].physical().key(Input::Key::Space).down);
    }

    TEST(EngineRunTest, RuntimeLifecycleFollowsOwnedSceneAndShutdownIsFinal) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        EXPECT_FALSE(engine.start_scene_runtime());
        EXPECT_FALSE(engine.set_runtime_state(SceneRuntime::State::Paused));
        EXPECT_FALSE(engine.request_runtime_step());
        auto calls = std::make_shared<RuntimeCalls>();
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.set_runtime_settings({.fixed_delta = 0.02}));
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.start_scene_runtime());
        EXPECT_EQ(calls->started_scene, engine.get_scene());
        EXPECT_FALSE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        EXPECT_FALSE(engine.set_runtime_settings({}));

        auto previous = engine.replace_scene(std::make_unique<Scene>());
        EXPECT_FALSE(engine.get_scene_runtime().is_active());
        EXPECT_EQ(calls->stops, 1);
        EXPECT_EQ(calls->stopped_scene, previous.get());
        ASSERT_TRUE(engine.start_scene_runtime());
        EXPECT_EQ(calls->started_scene, engine.get_scene());
        EXPECT_EQ(calls->starts, 2);
        ASSERT_TRUE(engine.stop_scene_runtime());
        ASSERT_TRUE(engine.stop_scene_runtime());
        EXPECT_EQ(calls->stops, 2);
        ASSERT_TRUE(engine.start_scene_runtime());
        engine.prepare_shutdown();
        EXPECT_EQ(calls->stops, 3);
        EXPECT_EQ(calls->stopped_scene, engine.get_scene());
        EXPECT_FALSE(engine.start_scene_runtime());
        EXPECT_FALSE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        EXPECT_FALSE(engine.set_runtime_settings({}));
    }

    TEST(EngineRunTest, PausedRuntimeKeepsHostAndRenderingAliveWhileStepRunsOnce) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        ASSERT_TRUE(engine.set_runtime_state(SceneRuntime::State::Paused));
        int hosts = 0;
        int ui_frames = 0;
        int draws = 0;
        Comet::Tests::set_test_overlay(
            engine.get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine.run({
            .update =
                [&](const Engine::FrameContext&) {
                    ++hosts;
                    EXPECT_EQ(calls->starts, 1);
                    EXPECT_EQ(calls->stops, 0);
                    EXPECT_EQ(calls->updates, hosts <= 2 ? 0 : 1);
                    EXPECT_EQ(calls->fixed_updates, hosts <= 2 ? 0 : 1);
                    EXPECT_EQ(engine.get_scene_runtime().get_state(), SceneRuntime::State::Paused);
                    if(hosts > 8)
                        engine.get_window().request_close();
                    if(hosts == 2)
                        return engine.request_runtime_step();
                    if(hosts == 4)
                        return engine.set_runtime_state(SceneRuntime::State::Running);
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    if(++ui_frames == 4)
                        engine.get_window().request_close();
                    return Result<void, Error>::success();
                },
        });
        engine.get_renderer().set_overlay({});
        ASSERT_TRUE(result);
        EXPECT_EQ(hosts, 4);
        EXPECT_EQ(ui_frames, 4);
        EXPECT_EQ(draws, 4);
        EXPECT_EQ(calls->updates, 2);
        EXPECT_EQ(engine.get_scene_runtime().get_state(), SceneRuntime::State::Running);
        engine.prepare_shutdown();
        EXPECT_EQ(calls->stops, 1);
        EXPECT_FALSE(engine.set_runtime_state(SceneRuntime::State::Paused));
        EXPECT_FALSE(engine.request_runtime_step());
    }

    TEST(EngineRunTest, UpdateCanCloseBeforeRenderingAndRejectsReentry) {
        Config config;
        auto engine_result = Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        int updates = 0;
        int preparations = 0;
        int inputs = 0;
        EXPECT_TRUE(engine.run({
            .update =
                [&](const Engine::FrameContext& frame) {
                    ++updates;
                    EXPECT_EQ(frame.physical_input.serial, 1u);
                    const auto nested = engine.run();
                    EXPECT_FALSE(nested);
                    if(!nested) {
                        EXPECT_EQ(nested.error().message, "Engine update loop is already running");
                        EXPECT_FALSE(nested.error().code);
                    }
                    EXPECT_FALSE(engine.run());
                    engine.get_window().request_close();
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    ++preparations;
                    return Result<void, Error>::success();
                },
            .runtime_input = [&](const Engine::FrameContext&) -> std::optional<Input::Frame> {
                ++inputs;
                return std::nullopt;
            },
        }));
        EXPECT_EQ(updates, 1);
        EXPECT_EQ(preparations, 0);
        EXPECT_EQ(inputs, 0);
    }

    TEST(EngineRunTest, DeferredFrameSkipsEditingAndDrawingButContinuesUpdates) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        auto& runtime = engine.get_scene_runtime();
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        auto& renderer = engine.get_renderer();
        auto* window = engine.get_window().get();
        const auto key = glfwSetKeyCallback(window, nullptr);
        glfwSetKeyCallback(window, key);
        const auto focus = glfwSetWindowFocusCallback(window, nullptr);
        glfwSetWindowFocusCallback(window, focus);
        ASSERT_TRUE(key && focus);
        int updates = 0;
        int edits = 0;
        int draws = 0;
        int rebuilds = 0;
        int input_calls = 0;
        Comet::Tests::set_test_overlay(
            renderer, {.render =
                              [&](OverlayRecordContext&) {
                                  ++draws;
                                  return Result<void, GraphicsError>::success();
                              },
                          .release = [] {},
                          .rebuild =
                              [&](const SwapchainCompatibility&) {
                                  ++rebuilds;
                                  return Result<void, GraphicsError>::failure(
                                      {"UI target is out of date", vk::Result::eErrorOutOfDateKHR});
                              }});
        renderer.request_swapchain_recreation();
        const auto result = engine.run({
            .update =
                [&](const Engine::FrameContext& frame) {
                    EXPECT_EQ(frame.physical_input.serial, static_cast<uint64_t>(updates + 1));
                    if(++updates == 1) {
                        focus(window, GLFW_TRUE);
                        key(window, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
                        key(window, GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
                        EXPECT_FALSE(frame.physical_input.key(Input::Key::Space).pressed);
                    } else {
                        EXPECT_TRUE(frame.physical_input.key(Input::Key::Space).pressed);
                        EXPECT_TRUE(frame.physical_input.key(Input::Key::Space).released);
                        engine.get_window().request_close();
                    }
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    ++edits;
                    return Result<void, Error>::success();
                },
            .runtime_input = [&](const Engine::FrameContext& frame) -> std::optional<Input::Frame> {
                ++input_calls;
                EXPECT_EQ(edits, 0);
                EXPECT_EQ(calls->updates, 0);
                auto routed = frame.physical_input;
                routed.focused = true;
                return routed;
            },
        });
        renderer.set_overlay({});
        EXPECT_TRUE(result);
        EXPECT_EQ(updates, 2);
        EXPECT_EQ(rebuilds, 1);
        EXPECT_EQ(edits, 0);
        EXPECT_EQ(input_calls, 1);
        EXPECT_EQ(draws, 0);
        EXPECT_EQ(calls->updates, 1);
        EXPECT_TRUE(calls->input_focused);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
    }

    TEST_F(EngineSceneActivationTest, PresentationOutOfMemoryStopsRuntimeAndPreservesError) {
        auto calls = std::make_shared<RuntimeCalls>();
        engine->set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine->add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine->start_scene_runtime());
        auto& renderer = engine->get_renderer();
        int updates = 0;
        int edits = 0;
        int rebuilds = 0;
        renderer.set_overlay({.rebuild = [&](const SwapchainCompatibility&) {
            ++rebuilds;
            return Result<void, GraphicsError>::failure(
                {"UI memory exhausted", vk::Result::eErrorOutOfDeviceMemory});
        }});
        renderer.request_swapchain_recreation();
        const auto result = engine->run({
            .update =
                [&](const Engine::FrameContext&) {
                    ++updates;
                    return Result<void, Error>::success();
                },
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    ++edits;
                    return Result<void, Error>::success();
                },
        });
        renderer.set_overlay({});
        ASSERT_FALSE(result);
        const auto expected_error =
            GraphicsError{"UI memory exhausted", vk::Result::eErrorOutOfDeviceMemory}.as_error();
        EXPECT_EQ(result.error().message, expected_error.message);
        EXPECT_EQ(result.error().code, expected_error.code);
        EXPECT_EQ(updates, 1);
        EXPECT_EQ(rebuilds, 1);
        EXPECT_EQ(edits, 0);
        EXPECT_EQ(calls->updates, 0);
        EXPECT_FALSE(engine->get_scene_runtime().is_active());
        EXPECT_FALSE(renderer.get_frame_scheduler().is_frame_active());
        EXPECT_FALSE(renderer.prepare_frame());
    }

    TEST(EngineRunTest, RuntimeFailureDrainsCapturedFrameAndStopsSystemsBeforeReturning) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        auto calls = std::make_shared<RuntimeCalls>();
        calls->fail_update = true;
        engine.set_scene(std::make_unique<Scene>());
        auto& runtime = engine.get_scene_runtime();
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        int draws = 0;
        Comet::Tests::set_test_overlay(
            engine.get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine.run();
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "runtime update failed");
        EXPECT_EQ(calls->updates, 1);
        EXPECT_EQ(calls->stops, 1);
        EXPECT_EQ(draws, 1);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(engine.run());
        engine.get_renderer().set_overlay({});
    }

    TEST(EngineRunTest, RuntimeRecoveryCompletesAcquiredFrameBeforeReplacingScene) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created);
        auto& engine = *created.value();
        engine.set_scene(std::make_unique<Scene>());
        const auto original = engine.get_scene();
        auto calls = std::make_shared<RuntimeCalls>();
        calls->fail_update = true;
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        int draws = 0;
        int recoveries = 0;
        Comet::Tests::set_test_overlay(
            engine.get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;
                return Result<void, GraphicsError>::success();
            }});
        const auto result = engine.run({
            .update =
                [&](const Engine::FrameContext&) {
                    if(draws >= 2)
                        engine.get_window().request_close();
                    return Result<void, Error>::success();
                },
            .runtime_failed =
                [&](const Error& error) {
                    ++recoveries;
                    EXPECT_EQ(error.message, "runtime update failed");
                    EXPECT_EQ(draws, 1);
                    EXPECT_FALSE(engine.get_renderer().get_frame_scheduler().is_frame_active());
                    EXPECT_FALSE(engine.get_scene_runtime().is_active());
                    EXPECT_EQ(calls->stops, 1);
                    auto previous = engine.replace_scene(std::make_unique<Scene>());
                    EXPECT_EQ(previous.get(), original);
                    return Result<void, Error>::success();
                },
        });
        EXPECT_TRUE(result);
        EXPECT_EQ(recoveries, 1);
        EXPECT_EQ(draws, 2);
        engine.get_renderer().set_overlay({});
    }

    TEST(EngineRunTest, FrameReadyFailureStopsEngineWithoutDrawingOrReusingAcquiredFrame) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        int edits = 0;
        int draws = 0;
        Comet::Tests::set_test_overlay(
            engine.get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;
                return Result<void, GraphicsError>::success();
            }});
        const Error failure =
            GraphicsError{"asset creation failed", vk::Result::eErrorDeviceLost}.as_error();
        const auto result = engine.run({
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    ++edits;
                    return Result<void, Error>::failure(failure);
                },
        });
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, failure.message);
        EXPECT_EQ(result.error().code, failure.code);
        EXPECT_EQ(edits, 1);
        EXPECT_EQ(draws, 0);
        EXPECT_FALSE(engine.get_task_scheduler().try_submit([] {}));
        EXPECT_FALSE(engine.get_renderer().prepare_frame());
        const auto restarted = engine.run();
        ASSERT_FALSE(restarted);
        EXPECT_EQ(restarted.error().message, "Engine is shutting down");
        engine.get_renderer().set_overlay({});
    }

    TEST(EngineRunTest, ShutdownPreparationDrainsWorkAndRejectsNewFrames) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        bool completed = false;
        auto task = engine.get_task_scheduler().try_submit([&] { completed = true; });
        ASSERT_TRUE(task);
        engine.prepare_shutdown();
        engine.prepare_shutdown();
        EXPECT_TRUE(completed);
        EXPECT_FALSE(engine.get_task_scheduler().try_submit([] {}));
        const auto restarted = engine.run();
        ASSERT_FALSE(restarted);
        EXPECT_EQ(restarted.error().message, "Engine is shutting down");
        EXPECT_FALSE(engine.get_renderer().prepare_frame());
        EXPECT_FALSE(engine.get_renderer().render_frame({}));
    }

    TEST(EngineRunTest, RendererFailureAlsoStopsRuntimeAndTaskSubmission) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error();
        auto& engine = *created.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        const auto result = engine.run({
            .frame_ready =
                [&](const Engine::FrameContext&) {
                    engine.get_renderer().prepare_shutdown();
                    return Result<void, Error>::success();
                },
        });
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "Renderer is shutting down");
        EXPECT_EQ(calls->stops, 1);
        EXPECT_FALSE(engine.get_scene_runtime().is_active());
        EXPECT_FALSE(engine.get_task_scheduler().try_submit([] {}));
        EXPECT_FALSE(engine.run());
    }

    TEST(EngineRunTest, CallbackFailureDoesNotLeaveLoopRunning) {
        Config config;
        auto engine_result = Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        const auto failure = engine.run({
            .update =
                [](const Engine::FrameContext&) {
                    return Result<void, Error>::failure(
                        GraphicsError{"update failed", vk::Result::eErrorDeviceLost}.as_error());
                },
        });
        ASSERT_FALSE(failure);
        EXPECT_EQ(failure.error().message, "update failed");
        EXPECT_EQ(failure.error().code,
            (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        int updates = 0;
        EXPECT_TRUE(engine.run({
            .update =
                [&](const Engine::FrameContext&) {
                    ++updates;
                    engine.get_window().request_close();
                    return Result<void, Error>::success();
                },
        }));
        EXPECT_EQ(updates, 1);
    }

    TEST(EngineRunTest, FailedUpdateDiscardsPreviousFrameDiagnostics) {
        Config config;
        config.diagnostics.enable_render_diagnostics = true;
        auto created = Engine::create(config);
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        int updates = 0;
        int draws = 0;
        Comet::Tests::set_test_overlay(
            engine.get_renderer(), {.render = [&](OverlayRecordContext&) {
                ++draws;
                return Result<void, GraphicsError>::success();
            }});

        const auto result = engine.run({
            .update =
                [&](const Engine::FrameContext&) {
                    if(++updates == 1)
                        return Result<void, Error>::success();
                    EXPECT_TRUE(engine.frame_diagnostics().current().has_value());
                    return Result<void, Error>::failure({"update failed"});
                },
        });

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "update failed");
        EXPECT_EQ(updates, 2);
        EXPECT_EQ(draws, 1);
        EXPECT_FALSE(engine.frame_diagnostics().current().has_value());
        engine.get_renderer().set_overlay({});
    }

}
