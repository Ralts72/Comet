#include "scene/scene_document.h"
#include "support/temporary_directory.h"
#include "common/file_io.h"

#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace CometEditor::Tests {
    namespace {
        const Comet::ComponentRegistry& component_registry() {
            static const Comet::ComponentRegistry registry =
                Comet::create_scene_component_registry();
            return registry;
        }

        class TemporarySceneFile final {
        public:
            TemporarySceneFile() {
                std::filesystem::create_directories(m_directory.path() / "assets");
            }

            [[nodiscard]] std::string path() const {
                return (m_directory.path() / "assets/untitled.scene").string();
            }
            [[nodiscard]] Comet::ProjectPaths paths() const {
                return Comet::ProjectPaths(m_directory.path());
            }

        private:
            Comet::Tests::TemporaryDirectory m_directory;
        };
    }

    TEST(SceneDocumentTest, SavedStateTracksUndoBranchesAndFailedSaves) {
        const auto& registry = component_registry();
        const Comet::SceneSerializer serializer(registry);
        const TemporarySceneFile file;
        auto active = std::make_unique<Comet::Scene>();
        auto entity = active->create_entity();
        CommandHistory history;
        history.bind_scene(active.get());
        SceneDocument document(
            serializer, file.paths(), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> candidate) {
                active.swap(candidate);
                history.bind_scene(active.get());
                return candidate;
            });
        PropertyEditTransaction edit(history, registry);
        const PropertyEditTransaction::Target target{entity.get_uuid(), "transform", "translation"};
        ASSERT_TRUE(edit.apply(target, Comet::Math::Vec3(1)));
        EXPECT_TRUE(document.is_modified());
        ASSERT_TRUE(document.save(file.path()));
        EXPECT_FALSE(document.is_modified());
        ASSERT_TRUE(edit.apply(target, Comet::Math::Vec3(2)));
        EXPECT_TRUE(document.is_modified());
        EXPECT_FALSE(document.save(""));
        EXPECT_TRUE(document.is_modified());
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(document.is_modified());
        ASSERT_TRUE(history.redo());
        EXPECT_TRUE(document.is_modified());
        document.request({SceneDocument::Action::Open, file.path()});
        EXPECT_TRUE(document.needs_confirmation());
        EXPECT_FALSE(document.take_ready_request());
        document.decide(SceneDocument::Decision::Save);
        EXPECT_FALSE(document.needs_confirmation());
        EXPECT_FALSE(document.save(""));
        EXPECT_FALSE(document.take_ready_request());
        ASSERT_TRUE(document.save(file.path()));
        const auto next = document.take_ready_request();
        ASSERT_TRUE(next);
        EXPECT_EQ(next->action, SceneDocument::Action::Open);
        EXPECT_EQ(next->path, file.path());
        EXPECT_FALSE(document.take_ready_request());
        ASSERT_TRUE(edit.apply(target, Comet::Math::Vec3(3)));
        document.request({SceneDocument::Action::Close, {}});
        document.request({SceneDocument::Action::New, {}});
        document.decide(SceneDocument::Decision::Cancel);
        EXPECT_FALSE(document.has_pending_request());
        EXPECT_TRUE(document.is_modified());
        document.request({SceneDocument::Action::Close, {}});
        document.decide(SceneDocument::Decision::Discard);
        const auto close = document.take_ready_request();
        ASSERT_TRUE(close);
        EXPECT_EQ(close->action, SceneDocument::Action::Close);
        EXPECT_TRUE(document.is_modified());
        ASSERT_TRUE(document.create_new());
        EXPECT_FALSE(document.is_modified());
    }

    TEST(SceneDocumentTest, PreparationFailurePreservesSceneAndDocumentPath) {
        const Comet::SceneSerializer serializer(component_registry());
        const TemporarySceneFile file;
        Comet::Scene scene;
        ASSERT_TRUE(serializer.save(scene, file.path()));
        auto active = std::make_unique<Comet::Scene>();
        const auto original = active.get();
        int installations = 0;
        const Comet::Error preparation_error{
            "candidate preparation rejected", std::make_error_code(std::errc::io_error)};
        CommandHistory history;
        SceneDocument document(
            serializer, file.paths(), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> candidate) {
                ++installations;
                active.swap(candidate);
                return candidate;
            },
            [&](Comet::Scene& candidate) {
                EXPECT_NE(&candidate, original);
                EXPECT_EQ(active.get(), original);
                return Comet::Result<void, Comet::Error>::failure(preparation_error);
            });
        ASSERT_TRUE(document.save(file.path()));
        const auto opened = document.open(file.path());
        ASSERT_FALSE(opened);
        EXPECT_EQ(opened.error().code, preparation_error.code);
        const auto created = document.create_new();
        ASSERT_FALSE(created);
        EXPECT_EQ(created.error().code, preparation_error.code);
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(installations, 0);
        EXPECT_EQ(document.get_path(), file.path());
        EXPECT_EQ(document.get_last_error(), "candidate preparation rejected");
    }

    TEST(SceneDocumentTest, OpenPreservesUnresolvedAssetReferences) {
        const Comet::SceneSerializer serializer(component_registry());
        const auto missing_mesh = Comet::AssetHandle::generate();
        const auto missing_material = Comet::AssetHandle::generate();
        Comet::Scene saved;
        auto entity = saved.create_entity("Unresolved assets");
        entity.add_component<Comet::MeshRendererComponent>(missing_mesh, missing_material);
        const auto uuid = entity.get_uuid();
        const TemporarySceneFile file;
        ASSERT_TRUE(serializer.save(saved, file.path()));

        auto active = std::make_unique<Comet::Scene>();
        CommandHistory history;
        SceneDocument document(
            serializer, file.paths(), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                active.swap(replacement);
                return replacement;
            });
        ASSERT_TRUE(document.open(file.path()));
        EXPECT_EQ(document.get_path(), file.path());
        EXPECT_TRUE(document.get_last_error().empty());
        const auto restored = active->find_entity(uuid);
        ASSERT_TRUE(restored);
        const auto& renderer = restored.get_component<Comet::MeshRendererComponent>();
        EXPECT_EQ(renderer.mesh, missing_mesh);
        EXPECT_EQ(renderer.material, missing_material);
    }

    TEST(SceneDocumentTest, FailedStartupOpenAllowsEmptySceneWithoutOverwritingFile) {
        const Comet::SceneSerializer serializer(component_registry());
        std::unique_ptr<Comet::Scene> active;
        const TemporarySceneFile file;
        CommandHistory history;
        SceneDocument document(
            serializer, file.paths(), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                active.swap(replacement);
                return replacement;
            });
        ASSERT_FALSE(document.open(file.path()));
        EXPECT_EQ(active, nullptr);
        ASSERT_TRUE(document.create_new());
        EXPECT_EQ(active->entity_count(), 0U);
        EXPECT_TRUE(document.get_path().empty());
        EXPECT_FALSE(std::filesystem::exists(file.path()));

        const std::string invalid = "invalid scene";
        std::ofstream(file.path()) << invalid;
        ASSERT_FALSE(document.open(file.path()));
        EXPECT_FALSE(document.get_last_error().empty());
        ASSERT_TRUE(document.create_new());
        EXPECT_EQ(active->entity_count(), 0U);
        EXPECT_TRUE(document.get_path().empty());
        std::ifstream input(file.path());
        std::string contents;
        std::getline(input, contents);
        EXPECT_EQ(contents, invalid);
    }

    TEST(SceneDocumentTest, ResolvesRelativePathsAndRejectsFilesOutsideProjectAssets) {
        const Comet::SceneSerializer serializer(component_registry());
        const TemporarySceneFile file;
        auto active = std::make_unique<Comet::Scene>();
        active->create_entity("Keep Me");
        CommandHistory history;
        SceneDocument document(
            serializer, file.paths(), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                active.swap(replacement);
                return replacement;
            });
        ASSERT_TRUE(document.save("scenes/saved.scene"));
        const auto saved_path = document.get_path();
        EXPECT_EQ(saved_path, (file.paths().assets() / "scenes/saved.scene").string());
        ASSERT_TRUE(document.open("scenes/saved.scene"));
        const auto outside = file.paths().root() / "outside.scene";
        ASSERT_TRUE(serializer.save(*active, outside.string()));
        const auto original = Comet::read_text_file(outside);
        ASSERT_TRUE(original) << original.error();
        const std::string invalid_paths[]{"../outside.scene", outside.string(), "wrong.mat"};
        for(const auto& path : invalid_paths) {
            EXPECT_FALSE(document.open(path));
            EXPECT_FALSE(document.save(path));
            EXPECT_EQ(document.get_path(), saved_path);
            EXPECT_EQ(active->entity_count(), 1U);
        }
        const auto stored = Comet::read_text_file(outside);
        ASSERT_TRUE(stored) << stored.error();
        EXPECT_EQ(stored.value(), original.value());
        EXPECT_FALSE(std::filesystem::exists(file.paths().assets() / "wrong.mat"));
    }

    TEST(SceneDocumentTest, OwnsScenePersistenceLifecycle) {
        const Comet::SceneSerializer serializer(component_registry());
        auto active_scene = std::make_unique<Comet::Scene>();
        active_scene->create_entity("Saved Entity");
        const TemporarySceneFile file;
        CommandHistory history;
        SceneDocument document(
            serializer, file.paths(), history, [&active_scene]() { return active_scene.get(); },
            [&active_scene](std::unique_ptr<Comet::Scene> replacement) {
                active_scene.swap(replacement);
                return replacement;
            });
        ASSERT_TRUE(document.save(file.path()));
        EXPECT_EQ(document.get_path(), file.path());
        EXPECT_TRUE(document.get_last_error().empty());

        active_scene->create_entity("Unsaved Entity");
        ASSERT_EQ(active_scene->entity_count(), 2U);
        ASSERT_TRUE(document.open(file.path()));
        EXPECT_EQ(active_scene->entity_count(), 1U);
        EXPECT_EQ(document.get_path(), file.path());

        ASSERT_TRUE(document.create_new());
        EXPECT_EQ(active_scene->entity_count(), 0U);
        EXPECT_TRUE(document.get_path().empty());
    }

    TEST(SceneDocumentTest, FailedPersistenceKeepsScenePathAndExistingFile) {
        const Comet::SceneSerializer serializer(component_registry());
        const TemporarySceneFile file;
        auto active = std::make_unique<Comet::Scene>();
        auto entity = active->create_entity("Valid");
        const auto original = active.get();
        CommandHistory history;
        SceneDocument document(
            serializer, file.paths(), history, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                active.swap(replacement);
                return replacement;
            });
        ASSERT_TRUE(document.save(file.path()));
        const auto contents = Comet::read_text_file(file.path());
        ASSERT_TRUE(contents) << contents.error();
        entity.get_component<Comet::NameComponent>().name = std::string(1, '\xff');
        EXPECT_FALSE(document.save(file.path()));
        EXPECT_FALSE(document.save("other.scene"));
        EXPECT_EQ(document.get_path(), file.path());
        EXPECT_FALSE(std::filesystem::exists(file.paths().assets() / "other.scene"));
        const auto preserved = Comet::read_text_file(file.path());
        ASSERT_TRUE(preserved) << preserved.error();
        EXPECT_EQ(preserved.value(), contents.value());

        ASSERT_TRUE(Comet::write_text_file_atomic(file.paths().assets() / "broken.scene", "{}"));
        EXPECT_FALSE(document.open("broken.scene"));
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(document.get_path(), file.path());
        EXPECT_FALSE(document.get_last_error().empty());
    }
}
