#include "scene_document.h"
#include "common/file_io.h"

#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <string>
#include <utility>

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
                const auto id = std::random_device{}();
                m_root =
                    std::filesystem::canonical(std::filesystem::temp_directory_path())
                    / ("comet_scene_document_" + std::to_string(id));
                std::filesystem::create_directories(m_root / "assets");
                m_path = m_root / "assets/untitled.scene";
            }

            ~TemporarySceneFile() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] std::string path() const { return m_path.string(); }
            [[nodiscard]] Comet::ProjectPaths paths() const {
                return Comet::ProjectPaths(m_root);
            }

        private:
            std::filesystem::path m_path;
            std::filesystem::path m_root;
        };
    }

    TEST(SceneDocumentTest, OpenPreservesUnresolvedAssetReferences) {
        const Comet::SceneSerializer serializer(component_registry());
        const auto missing_mesh = Comet::AssetHandle::generate();
        const auto missing_material = Comet::AssetHandle::generate();
        Comet::Scene saved;
        auto entity = saved.create_entity("Unresolved assets");
        entity.add_component<Comet::MeshRendererComponent>(
            missing_mesh, missing_material);
        const auto uuid = entity.get_uuid();
        const TemporarySceneFile file;
        serializer.save(saved, file.path());

        auto active = std::make_unique<Comet::Scene>();
        SceneDocument document(
            serializer, file.paths(), [&] { return active.get(); },
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
        SceneDocument document(
            serializer, file.paths(), [&] { return active.get(); },
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
        SceneDocument document(
            serializer, file.paths(), [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                active.swap(replacement);
                return replacement;
            });
        ASSERT_TRUE(document.save("scenes/saved.scene"));
        const auto saved_path = document.get_path();
        EXPECT_EQ(saved_path, (file.paths().assets() / "scenes/saved.scene").string());
        ASSERT_TRUE(document.open("scenes/saved.scene"));
        const auto outside = file.paths().root() / "outside.scene";
        serializer.save(*active, outside.string());
        const auto original = Comet::read_text_file(outside);
        const std::string invalid_paths[]{
            "../outside.scene", outside.string(), "wrong.mat"};
        for(const auto& path : invalid_paths) {
            EXPECT_FALSE(document.open(path));
            EXPECT_FALSE(document.save(path));
            EXPECT_EQ(document.get_path(), saved_path);
            EXPECT_EQ(active->entity_count(), 1U);
        }
        EXPECT_EQ(Comet::read_text_file(outside), original);
        EXPECT_FALSE(std::filesystem::exists(file.paths().assets() / "wrong.mat"));
    }

    TEST(SceneDocumentTest, OwnsScenePersistenceLifecycle) {
        const Comet::SceneSerializer serializer(component_registry());
        auto active_scene = std::make_unique<Comet::Scene>();
        active_scene->create_entity("Saved Entity");
        const TemporarySceneFile file;
        SceneDocument document(
            serializer, file.paths(), [&active_scene]() { return active_scene.get(); },
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
}
