#include "project/asset_operations.h"
#include "project/project_creation.h"
#include "project/project_session.h"
#include "assets/editor_assets.h"
#include "scene/scene_document.h"
#include "common/file_io.h"
#include "asset/registry.h"
#include "core/project.h"
#include "core/task_scheduler.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <utility>

namespace CometEditor::Tests {
    class ProjectAssetOperationsTest: public ::testing::Test {
    protected:
        Comet::Tests::TemporaryDirectory directory;
        const std::filesystem::path root = directory.path() / "Project";
        const std::filesystem::path initial = "scenes/main.scene";
        const std::filesystem::path destination = "levels/renamed.scene";
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::AssetRegistry registry;
        Comet::TaskScheduler scheduler{1};
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        Comet::SceneSerializer serializer{components};
        CommandHistory history;
        PropertyEditTransaction edit{history, components};
        std::unique_ptr<Comet::Scene> scene;
        std::optional<Comet::Project> project;
        std::unique_ptr<EditorAssets> assets;
        std::unique_ptr<SceneDocument> document;
        std::unique_ptr<ProjectSession> session;
        Comet::AssetHandle handle;
        int trash_requests = 0;

        void SetUp() override {
            ASSERT_TRUE(create_project(root));
            auto loaded = Comet::Project::load(root);
            ASSERT_TRUE(loaded);
            project.emplace(std::move(loaded).value());
            assets = std::make_unique<EditorAssets>(project->paths(), registry, factory, scheduler,
                DEFAULT_FILE_CHANGE_QUIET_PERIOD, Comet::AssetImportLimits{},
                [this](const std::filesystem::path& path) {
                    ++trash_requests;
                    std::error_code error;
                    std::filesystem::create_directories(root / "trash", error);
                    if(!error)
                        std::filesystem::rename(path, root / "trash" / path.filename(), error);
                    if(error)
                        return Comet::Result<void>::failure(error.message());
                    return Comet::Result<void>::success();
                });
            ASSERT_TRUE(assets->refresh().succeeded());
            handle = assets->database().find(initial)->handle;
            document = std::make_unique<SceneDocument>(
                serializer, project->paths(), history, [this] { return scene.get(); },
                [this](std::unique_ptr<Comet::Scene> candidate) {
                    scene = std::move(candidate);
                    history.bind_scene(scene.get());
                    return Comet::Result<void, Comet::Error>::success();
                });
            ASSERT_TRUE(document->open(initial.string()));
            session = std::make_unique<ProjectSession>(project->paths());
            ASSERT_TRUE(session->record_scene(initial));
        }

        Comet::AssetScanReport move() {
            return move_project_asset(*assets, *project, *document, *session, handle, destination);
        }

        void expect_original_paths() {
            EXPECT_EQ(assets->database().find(handle)->path, initial);
            EXPECT_TRUE(std::filesystem::exists(project->paths().assets() / initial));
            EXPECT_TRUE(
                std::filesystem::exists(Comet::metadata_path(project->paths().assets() / initial)));
            EXPECT_FALSE(std::filesystem::exists(project->paths().assets() / destination));
            EXPECT_EQ(project->startup_scene(), initial);
            EXPECT_EQ(document->get_asset_relative_path(), initial);
            EXPECT_EQ(session->last_scene(), initial);
        }
    };

    TEST_F(ProjectAssetOperationsTest, MovePreservesDirtyDocumentAndRelocatesProjectAndSession) {
        const auto entity = scene->get_entities().front();
        const auto uuid = entity.get_uuid();
        ASSERT_TRUE(edit.apply({uuid, "transform", "translation"}, Comet::Math::Vec3(2)));
        const auto generation = history.generation();
        const auto state = history.state_id();
        const auto* active = scene.get();
        const auto report = move();
        ASSERT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_EQ(scene.get(), active);
        EXPECT_EQ(history.generation(), generation);
        EXPECT_EQ(history.state_id(), state);
        EXPECT_TRUE(document->is_modified());
        EXPECT_EQ(document->get_asset_relative_path(), destination);
        EXPECT_EQ(assets->database().find(handle)->path, destination);
        EXPECT_EQ(Comet::Project::load(root).value().startup_scene(), destination);
        ProjectSession reopened(project->paths());
        ASSERT_TRUE(reopened.load());
        EXPECT_EQ(reopened.last_scene(), destination);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(document->is_modified());
        ASSERT_TRUE(history.redo());
        ASSERT_TRUE(document->save(document->get_path()));
        EXPECT_FALSE(std::filesystem::exists(project->paths().assets() / initial));
        const auto saved = serializer.load(document->get_path());
        ASSERT_TRUE(saved);
        EXPECT_EQ(
            saved.value()->find_entity(uuid).get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(2));
    }

    TEST_F(
        ProjectAssetOperationsTest, ProjectWriteFailureRollsBackMoveAndReportsLatestAssetSnapshot) {
        const std::filesystem::path discovered = "scenes/discovered.scene";
        std::filesystem::copy_file(
            project->paths().assets() / initial, project->paths().assets() / discovered);
        ASSERT_EQ(assets->database().find(discovered), nullptr);
        const auto manifest = root / "project.json";
        const auto backup = root / "saved.json";
        const auto original = Comet::read_text_file(manifest).value();
        std::filesystem::rename(manifest, backup);
        ASSERT_TRUE(std::filesystem::create_directory(manifest));
        const auto report = move();
        ASSERT_FALSE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        ASSERT_NE(assets->database().find(discovered), nullptr);
        EXPECT_EQ(report.indexed_assets, assets->database().size());
        EXPECT_EQ(report.indexed_assets, 2);
        ASSERT_FALSE(report.issues.empty());
        EXPECT_NE(report.issues.front().message.find("startup scene"), std::string::npos);
        expect_original_paths();
        EXPECT_TRUE(std::filesystem::is_directory(manifest));
        EXPECT_EQ(Comet::read_text_file(backup).value(), original);
    }

    TEST_F(ProjectAssetOperationsTest, SessionWriteFailureKeepsMovedFilesAndProject) {
        const auto session_path = project->paths().editor_state() / "session.json";
        ASSERT_TRUE(std::filesystem::remove(session_path));
        ASSERT_TRUE(std::filesystem::create_directory(session_path));
        const auto report = move();
        EXPECT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_EQ(assets->database().find(handle)->path, destination);
        EXPECT_TRUE(std::filesystem::exists(project->paths().assets() / destination));
        EXPECT_TRUE(
            std::filesystem::exists(Comet::metadata_path(project->paths().assets() / destination)));
        EXPECT_FALSE(std::filesystem::exists(project->paths().assets() / initial));
        EXPECT_FALSE(
            std::filesystem::exists(Comet::metadata_path(project->paths().assets() / initial)));
        EXPECT_EQ(project->startup_scene(), destination);
        EXPECT_EQ(Comet::Project::load(root).value().startup_scene(), destination);
        EXPECT_EQ(document->get_asset_relative_path(), destination);
        EXPECT_EQ(session->last_scene(), destination);

        ASSERT_TRUE(std::filesystem::remove(session_path));
        ASSERT_TRUE(session->record_scene(destination));
        ProjectSession reopened(project->paths());
        ASSERT_TRUE(reopened.load());
        EXPECT_EQ(reopened.last_scene(), destination);
    }

    TEST_F(ProjectAssetOperationsTest,
        CaseOnlySceneRenameUpdatesPathsAndRollsBackProjectWriteFailure) {
        const std::filesystem::path renamed = "scenes/Main.scene";
        const auto report =
            move_project_asset(*assets, *project, *document, *session, handle, renamed);
        ASSERT_TRUE(report.succeeded());
        EXPECT_EQ(document->get_asset_relative_path(), renamed);
        EXPECT_EQ(project->startup_scene(), renamed);
        EXPECT_EQ(session->last_scene(), renamed);
        EXPECT_EQ(assets->database().find(handle)->path, renamed);
        EXPECT_EQ(Comet::Project::load(root).value().startup_scene(), renamed);

        const auto manifest = root / "project.json";
        std::filesystem::rename(manifest, root / "saved.json");
        ASSERT_TRUE(std::filesystem::create_directory(manifest));
        const auto failed =
            move_project_asset(*assets, *project, *document, *session, handle, initial);
        EXPECT_FALSE(failed.succeeded());
        EXPECT_EQ(document->get_asset_relative_path(), renamed);
        EXPECT_EQ(project->startup_scene(), renamed);
        EXPECT_EQ(session->last_scene(), renamed);
        EXPECT_EQ(assets->database().find(handle)->path, renamed);
        for(const auto& entry :
            std::filesystem::directory_iterator(project->paths().assets() / "scenes")) {
            EXPECT_TRUE(entry.path().filename() == "Main.scene"
                        || entry.path().filename() == "Main.scene.meta")
                << entry.path();
        }
    }

    TEST_F(ProjectAssetOperationsTest, InvalidDestinationDoesNotUpdatePathReferences) {
        const auto report =
            move_project_asset(*assets, *project, *document, *session, handle, "../outside.scene");
        EXPECT_FALSE(report.succeeded());
        EXPECT_FALSE(report.snapshot_updated);
        expect_original_paths();
    }

    TEST_F(ProjectAssetOperationsTest, CurrentAndStartupScenesCannotBeDeletedButOthersCan) {
        const std::filesystem::path other = "scenes/other.scene";
        std::filesystem::copy_file(
            project->paths().assets() / initial, project->paths().assets() / other);
        ASSERT_TRUE(assets->refresh().succeeded());
        const auto other_handle = assets->database().find(other)->handle;
        ASSERT_TRUE(document->open(other.string()));
        for(const auto protected_handle : {handle, other_handle}) {
            const auto report =
                remove_project_asset(*assets, *project, *document, protected_handle);
            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
        }
        EXPECT_EQ(trash_requests, 0);
        ASSERT_TRUE(document->open(initial.string()));
        const auto report = remove_project_asset(*assets, *project, *document, other_handle);
        EXPECT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_EQ(trash_requests, 2);
        EXPECT_FALSE(assets->database().find(other_handle));
    }
}
