#include "assets/editor_assets.h"
#include "scene/editor_scene_session.h"
#include "scene/scene_document.h"
#include "scene/scene_editor.h"
#include "scene/selection.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    class SceneActivationTest: public ::testing::Test {
    protected:
        using Owner = std::unique_ptr<Comet::Scene>;
        using Activation = Comet::Result<Owner, Comet::Error>;
        Comet::Tests::TemporaryDirectory directory;
        Comet::ProjectPaths paths{directory.path()};
        Comet::AssetRegistry registry;
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::TaskScheduler scheduler{1};
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        Comet::SceneSerializer serializer{components};
        Owner active = std::make_unique<Comet::Scene>();
        CommandHistory history;
        PropertyEditTransaction edit{history, components};
        SelectionService selection{*active};
        EditorState state;
        Comet::SceneRuntime runtime;
        std::unique_ptr<EditorAssets> assets;
        std::unique_ptr<SceneEditor> editor;
        std::unique_ptr<SceneDocument> document;
        Comet::AssetHandle mesh;
        int installations = 0;

        void SetUp() override {
            std::filesystem::create_directories(paths.assets());
            std::filesystem::copy_file(
                std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf",
                paths.assets() / "cube.gltf");
            assets = std::make_unique<EditorAssets>(paths, registry, factory, scheduler);
            ASSERT_TRUE(assets->refresh().succeeded());
            mesh = assets->database().find("cube.gltf")->handle;
            ASSERT_TRUE(assets->update());
            scheduler.wait_idle();
            ASSERT_TRUE(assets->update());
            editor =
                std::make_unique<SceneEditor>(state, history, edit, components, selection, *assets);
            editor->bind_scene(*active, EditorMode::Edit);
            document = std::make_unique<SceneDocument>(
                serializer, paths, history, [this] { return active.get(); },
                [this](Owner candidate) {
                    auto result = activate(std::move(candidate), EditorMode::Edit);
                    if(!result)
                        return Comet::Result<void, Comet::Error>::failure(result.error());
                    return Comet::Result<void, Comet::Error>::success();
                });
        }

        void TearDown() override {
            EXPECT_TRUE(runtime.stop());
            document.reset();
            editor.reset();
            assets.reset();
            registry.clear();
        }

        Activation activate(Owner candidate, EditorMode mode) {
            if(auto prepared = assets->prepare_scene(*candidate, components); !prepared)
                return Activation::failure(prepared.error());
            return Activation::success(replace(std::move(candidate), mode));
        }

        Owner replace(Owner candidate, EditorMode mode) {
            EXPECT_TRUE(edit.cancel());
            EXPECT_TRUE(runtime.stop());
            active.swap(candidate);
            editor->bind_scene(*active, mode);
            ++installations;
            return candidate;
        }
    };

    TEST_F(SceneActivationTest, FailedOpenKeepsBindingsAndSuccessfulOpenOrNewRebindsOnce) {
        auto original = active.get();
        auto entity = active->create_entity("Original");
        selection.select_entity(entity.get_id());
        ASSERT_TRUE(document->save("original.scene"));
        ASSERT_TRUE(edit.apply({entity.get_uuid(), "name", "name"}, std::string("Edited")));
        const auto history_state = history.state_id();
        const auto generation = history.generation();
        Comet::Scene candidate;
        candidate.create_entity().add_component<Comet::MeshRendererComponent>(
            mesh, Comet::AssetHandle{});
        ASSERT_TRUE(serializer.save(candidate, (paths.assets() / "candidate.scene").string()));
        factory.fail_mesh_creation(true);
        factory.set_failure_result(vk::Result::eErrorDeviceLost);

        const auto rejected = document->open("candidate.scene");
        ASSERT_FALSE(rejected);
        EXPECT_TRUE(Comet::is_device_lost(rejected.error()));
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(&selection.get_scene(), original);
        EXPECT_EQ(selection.get_selected_entity_id(), entity.get_id());
        EXPECT_EQ(history.state_id(), history_state);
        EXPECT_EQ(history.generation(), generation);
        EXPECT_EQ(document->get_asset_relative_path(), "original.scene");
        EXPECT_TRUE(document->is_modified());
        EXPECT_EQ(installations, 0);

        // 可恢复的资源失败保留引用；后台修复仍能恢复它。
        factory.set_failure_result(vk::Result::eErrorOutOfDeviceMemory);
        ASSERT_TRUE(document->open("candidate.scene"));
        EXPECT_EQ(installations, 1);
        EXPECT_EQ(history.generation(), generation + 1);
        EXPECT_EQ(history.get_scene(), active.get());
        EXPECT_EQ(&selection.get_scene(), active.get());
        EXPECT_FALSE(selection.get_selected_entity());
        EXPECT_FALSE(history.can_undo());
        EXPECT_FALSE(document->is_modified());
        EXPECT_EQ(assets->restore_references().value(), 1U);

        ASSERT_TRUE(document->create_new());
        EXPECT_EQ(installations, 2);
        EXPECT_EQ(history.generation(), generation + 2);
        EXPECT_TRUE(document->get_path().empty());
        EXPECT_EQ(active->entity_count(), 0U);
        EXPECT_EQ(assets->restore_references().value(), 0U);
    }

    TEST_F(SceneActivationTest, FailedPlayStartAndStopRestoreEditBindingsWithoutReloadingAssets) {
        class Startup final: public Comet::System {
        public:
            bool fail = true;
            int stops = 0;
            Comet::Result<void, Comet::Error> on_start(Comet::Scene& scene) override {
                scene.create_entity("RuntimeOnly");
                if(fail)
                    return Comet::Result<void, Comet::Error>::failure({"Startup failed"});
                return Comet::Result<void, Comet::Error>::success();
            }
            void on_stop(Comet::Scene& scene) noexcept override {
                EXPECT_EQ(scene.entity_count(), 2U);
                ++stops;
            }
        };
        auto system = std::make_unique<Startup>();
        auto* probe = system.get();
        ASSERT_TRUE(runtime.add_system(std::move(system)));
        const auto original = active.get();
        auto entity = active->create_entity("Original");
        entity.add_component<Comet::MeshRendererComponent>(mesh, Comet::AssetHandle{});
        ASSERT_TRUE(document->save("original.scene"));
        ASSERT_TRUE(edit.apply({entity.get_uuid(), "name", "name"}, std::string("Edited")));
        const auto history_state = history.state_id();
        const auto generation = history.generation();
        EditorSceneSession session(
            state, serializer, [this] { return active.get(); },
            [this](Owner candidate) { return activate(std::move(candidate), EditorMode::Play); },
            [this](Owner retained) { return replace(std::move(retained), EditorMode::Edit); },
            [this](Comet::SceneRuntime::State initial) { return runtime.start(*active, initial); });
        session.request_mode(EditorMode::Play);
        EXPECT_FALSE(session.apply_mode_request());
        EXPECT_EQ(installations, 2);
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(history.state_id(), history_state);
        EXPECT_EQ(history.generation(), generation);
        EXPECT_EQ(&selection.get_scene(), original);
        EXPECT_TRUE(document->is_modified());
        EXPECT_EQ(probe->stops, 1);
        const auto creations = factory.mesh_creation_count();

        probe->fail = false;
        session.request_mode(EditorMode::Play);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(history.get_scene(), original);
        EXPECT_EQ(&selection.get_scene(), active.get());
        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(&selection.get_scene(), original);
        EXPECT_EQ(history.generation(), generation);
        EXPECT_EQ(factory.mesh_creation_count(), creations);
        EXPECT_EQ(probe->stops, 2);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "Original");
        EXPECT_FALSE(document->is_modified());
    }
}
