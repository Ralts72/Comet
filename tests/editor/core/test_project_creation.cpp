#include "project/project_creation.h"

#include "common/file_io.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(ProjectCreationTest, CreatesLoadableProjectWithStartupScene) {
        Comet::Tests::TemporaryDirectory directory;
        const auto root = directory.path() / "My Game";
        const auto created = create_project(root);
        ASSERT_TRUE(created) << created.error();
        EXPECT_EQ(created.value(), std::filesystem::canonical(root));

        const auto loaded = Comet::Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().name(), "My Game");
        EXPECT_EQ(loaded.value().startup_scene(), "scenes/main.scene");
        const auto components = Comet::create_scene_component_registry();
        const auto scene = Comet::SceneSerializer(components).load(
            (root / "assets/scenes/main.scene").string());
        ASSERT_TRUE(scene) << scene.error();
        EXPECT_EQ(scene.value()->entity_count(), 1U);
        const auto camera = scene.value()->get_root_entities().front();
        EXPECT_EQ(camera.get_component<Comet::NameComponent>().name, "MainCamera");
        EXPECT_TRUE(camera.get_component<Comet::CameraComponent>().primary);
    }

    TEST(ProjectCreationTest, ExistingDirectoryIsNeverOverwritten) {
        Comet::Tests::TemporaryDirectory directory;
        const auto sentinel = directory.path() / "keep.txt";
        ASSERT_TRUE(Comet::write_text_file_atomic(sentinel, "keep"));
        EXPECT_FALSE(create_project(directory.path()));
        const auto contents = Comet::read_text_file(sentinel);
        ASSERT_TRUE(contents);
        EXPECT_EQ(contents.value(), "keep");
        EXPECT_FALSE(std::filesystem::exists(directory.path() / "project.json"));
    }
}
