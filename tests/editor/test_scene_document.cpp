#include "scene_document.h"

#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <filesystem>
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
                m_path = std::filesystem::temp_directory_path()
                         / ("comet_scene_document_" + std::to_string(id) + ".scene");
            }

            ~TemporarySceneFile() {
                std::error_code error;
                std::filesystem::remove(m_path, error);
            }

            [[nodiscard]] std::string path() const { return m_path.string(); }

        private:
            std::filesystem::path m_path;
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
            serializer, [&] { return active.get(); },
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

    TEST(SceneDocumentTest, OwnsScenePersistenceLifecycle) {
        const Comet::SceneSerializer serializer(component_registry());
        auto active_scene = std::make_unique<Comet::Scene>();
        active_scene->create_entity("Saved Entity");
        SceneDocument document(
            serializer, [&active_scene]() { return active_scene.get(); },
            [&active_scene](std::unique_ptr<Comet::Scene> replacement) {
                active_scene.swap(replacement);
                return replacement;
            });
        const TemporarySceneFile file;

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
