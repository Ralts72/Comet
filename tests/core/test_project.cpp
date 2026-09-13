#include "core/project.h"
#include "asset/handle.h"
#include "common/file_io.h"

#include <gtest/gtest.h>
#include <utility>

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
            const auto saved = write_text_file_atomic(root / "project.json", contents);
            ASSERT_TRUE(saved) << saved.error();
        }
    };

    TEST_F(ProjectTest, LoadsDirectoryOrManifestAndKeepsSettingsProjectRelative) {
        write(R"({"version": 1, "name": "My Game", "startup_scene": "levels/main.scene"})");
        auto project_result = Project::load(root);
        ASSERT_TRUE(project_result) << project_result.error();
        auto project = std::move(project_result).value();
        EXPECT_EQ(project.name(), "My Game");
        EXPECT_EQ(project.paths().root(), root);
        EXPECT_EQ(project.startup_scene(), "levels/main.scene");
        const auto scene_path = project.paths().resolve_asset_path(project.startup_scene());
        ASSERT_TRUE(scene_path) << scene_path.error();
        EXPECT_EQ(scene_path.value(), root / "assets/levels/main.scene");
        for(const auto& input : {root / "project.json", std::filesystem::relative(root)}) {
            const auto loaded = Project::load(input);
            ASSERT_TRUE(loaded) << loaded.error();
            EXPECT_EQ(loaded.value().paths().root(), root);
        }
        EXPECT_FALSE(std::filesystem::exists(root / ".comet"));

        const auto copy = root / "relocated project";
        std::filesystem::create_directories(copy / "assets");
        std::filesystem::copy_file(root / "project.json", copy / "project.json");
        auto relocated_result = Project::load(copy);
        ASSERT_TRUE(relocated_result) << relocated_result.error();
        auto relocated = std::move(relocated_result).value();
        const auto relocated_scene =
            relocated.paths().resolve_asset_path(relocated.startup_scene());
        ASSERT_TRUE(relocated_scene) << relocated_scene.error();
        EXPECT_EQ(relocated_scene.value(), copy / "assets/levels/main.scene");
    }

    TEST_F(ProjectTest, OptionalSceneDoesNotFallBackToSampleAssets) {
        write(R"({"version": 1, "name": "Empty Game"})");
        auto project_result = Project::load(root);
        ASSERT_TRUE(project_result) << project_result.error();
        auto project = std::move(project_result).value();
        EXPECT_TRUE(project.startup_scene().empty());
        EXPECT_TRUE(std::filesystem::is_empty(root / "assets"));
    }

    TEST_F(ProjectTest, RejectsInvalidManifestAndDoesNotRewriteIt) {
        const std::string invalid[]{"[]", R"({"version": 2, "name": "Game"})",
            R"({"version": 0, "name": "Game"})", R"({"name": "Game"})",
            R"({"version": "1", "name": "Game"})", R"({"version": 1, "name": ""})",
            R"({"version": 1, "name": 42})", R"({"version": 1, "name": "Game", "extra": 1})",
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
            EXPECT_FALSE(Project::load(root));
            const auto stored = read_text_file(root / "project.json");
            ASSERT_TRUE(stored) << stored.error();
            EXPECT_EQ(stored.value(), contents);
        }
    }

    TEST_F(ProjectTest, MissingProjectOrAssetsFailsWithoutCreatingThem) {
        EXPECT_FALSE(Project::load(root));
        EXPECT_FALSE(std::filesystem::exists(root / "project.json"));
        EXPECT_FALSE(Project::load({}));
        write(R"({"version": 1, "name": "Game"})");
        ASSERT_TRUE(std::filesystem::remove(root / "assets"));
        EXPECT_FALSE(Project::load(root));
        EXPECT_FALSE(std::filesystem::exists(root / "assets"));
    }

    TEST_F(ProjectTest, ScenePathCannotEscapeThroughSymlink) {
        std::filesystem::create_directory(root / "outside");
        std::error_code error;
        std::filesystem::create_directory_symlink(root / "outside", root / "assets/link", error);
        if(error)
            GTEST_SKIP() << "Directory symlinks unavailable: " << error.message();
        write(R"({"version": 1, "name": "Game", "startup_scene": "link/main.scene"})");
        EXPECT_FALSE(Project::load(root));
    }

    TEST_F(ProjectTest, DoesNotFallBackToLegacyManifest) {
        ASSERT_TRUE(write_text_file_atomic(root / "project.yaml", "version: 1\nname: Legacy\n"));
        EXPECT_FALSE(Project::load(root));
        EXPECT_FALSE(std::filesystem::exists(root / "project.json"));
        EXPECT_FALSE(Project::load(root / "project.yaml"));
    }
}
