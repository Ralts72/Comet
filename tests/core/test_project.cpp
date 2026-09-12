#include "core/project.h"
#include "asset/handle.h"
#include "common/file_io.h"

#include <gtest/gtest.h>
#include <stdexcept>

namespace Comet::Tests {
    class ProjectTest: public ::testing::Test {
    protected:
        std::filesystem::path root =
            std::filesystem::canonical(std::filesystem::temp_directory_path())
            / ("comet_project_" + std::to_string(AssetHandle::generate().value()));

        void SetUp() override { std::filesystem::create_directories(root / "assets"); }
        void TearDown() override {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        void write(const std::string_view contents) {
            write_text_file_atomic(root / "project.json", contents);
        }
    };

    TEST_F(ProjectTest, LoadsDirectoryOrManifestAndKeepsSettingsProjectRelative) {
        write(
            R"({"version": 1, "name": "My Game", "startup_scene": "levels/main.scene"})");
        const auto project = Project::load(root);
        EXPECT_EQ(project.name(), "My Game");
        EXPECT_EQ(project.paths().root(), root);
        EXPECT_EQ(project.startup_scene(), "levels/main.scene");
        EXPECT_EQ(project.paths().resolve_asset_path(project.startup_scene()),
            root / "assets/levels/main.scene");
        EXPECT_EQ(Project::load(root / "project.json").paths().root(), root);
        EXPECT_EQ(Project::load(std::filesystem::relative(root)).paths().root(), root);
        EXPECT_FALSE(std::filesystem::exists(root / ".comet"));

        const auto copy = root / "relocated project";
        std::filesystem::create_directories(copy / "assets");
        std::filesystem::copy_file(root / "project.json", copy / "project.json");
        const auto relocated = Project::load(copy);
        EXPECT_EQ(relocated.paths().resolve_asset_path(relocated.startup_scene()),
            copy / "assets/levels/main.scene");
    }

    TEST_F(ProjectTest, OptionalSceneDoesNotFallBackToSampleAssets) {
        write(R"({"version": 1, "name": "Empty Game"})");
        const auto project = Project::load(root);
        EXPECT_TRUE(project.startup_scene().empty());
        EXPECT_TRUE(std::filesystem::is_empty(root / "assets"));
    }

    TEST_F(ProjectTest, RejectsInvalidManifestAndDoesNotRewriteIt) {
        const std::string invalid[]{"[]", R"({"version": 2, "name": "Game"})",
            R"({"version": 0, "name": "Game"})", R"({"name": "Game"})",
            R"({"version": "1", "name": "Game"})", R"({"version": 1, "name": ""})",
            R"({"version": 1, "name": 42})",
            R"({"version": 1, "name": "Game", "extra": 1})",
            R"({"version": 1, "name": "Game", "name": "Duplicate"})",
            R"({"version": 1, "name": "Game", "startup_scene": "../outside.scene"})",
            R"({"version": 1, "name": "Game", "startup_scene": "/outside.scene"})",
            R"({"version": 1, "name": "Game", "startup_scene": "texture.png"})",
            R"({"version": 1, "name": "Game", "startup_scene": null})",
            R"({"version": 1, "name": "Game", "default_material": 123})",
            "version: 1\nname: Game\n"};
        for(const auto& contents : invalid) {
            SCOPED_TRACE(contents);
            write(contents);
            EXPECT_THROW(static_cast<void>(Project::load(root)), std::runtime_error);
            EXPECT_EQ(read_text_file(root / "project.json"), contents);
        }
    }

    TEST_F(ProjectTest, MissingProjectOrAssetsFailsWithoutCreatingThem) {
        EXPECT_THROW(
            static_cast<void>(Project::load(root)), std::filesystem::filesystem_error);
        EXPECT_FALSE(std::filesystem::exists(root / "project.json"));
        EXPECT_THROW(static_cast<void>(Project::load({})), std::runtime_error);
        write(R"({"version": 1, "name": "Game"})");
        ASSERT_TRUE(std::filesystem::remove(root / "assets"));
        EXPECT_THROW(static_cast<void>(Project::load(root)), std::runtime_error);
        EXPECT_FALSE(std::filesystem::exists(root / "assets"));
    }

    TEST_F(ProjectTest, ScenePathCannotEscapeThroughSymlink) {
        std::filesystem::create_directory(root / "outside");
        std::error_code error;
        std::filesystem::create_directory_symlink(
            root / "outside", root / "assets/link", error);
        if(error)
            GTEST_SKIP() << "Directory symlinks unavailable: " << error.message();
        write(R"({"version": 1, "name": "Game", "startup_scene": "link/main.scene"})");
        EXPECT_THROW(static_cast<void>(Project::load(root)), std::runtime_error);
    }

    TEST_F(ProjectTest, DoesNotFallBackToLegacyManifest) {
        write_text_file_atomic(root / "project.yaml", "version: 1\nname: Legacy\n");
        EXPECT_THROW(
            static_cast<void>(Project::load(root)), std::filesystem::filesystem_error);
        EXPECT_FALSE(std::filesystem::exists(root / "project.json"));
        EXPECT_THROW(
            static_cast<void>(Project::load(root / "project.yaml")), std::runtime_error);
    }
}
