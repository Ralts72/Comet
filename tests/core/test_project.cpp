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
            write_text_file_atomic(root / "project.yaml", contents);
        }
    };

    TEST_F(ProjectTest, LoadsDirectoryOrManifestAndKeepsSettingsProjectRelative) {
        write("version: 1\nname: My Game\nstartup_scene: levels/main.scene\n");
        const auto project = Project::load(root);
        EXPECT_EQ(project.name(), "My Game");
        EXPECT_EQ(project.paths().root(), root);
        EXPECT_EQ(project.startup_scene(), "levels/main.scene");
        EXPECT_EQ(project.paths().resolve_asset_path(project.startup_scene()),
            root / "assets/levels/main.scene");
        EXPECT_EQ(Project::load(root / "project.yaml").paths().root(), root);
        EXPECT_EQ(Project::load(std::filesystem::relative(root)).paths().root(), root);
        EXPECT_FALSE(std::filesystem::exists(root / ".comet"));

        const auto copy = root / "relocated project";
        std::filesystem::create_directories(copy / "assets");
        std::filesystem::copy_file(root / "project.yaml", copy / "project.yaml");
        const auto relocated = Project::load(copy);
        EXPECT_EQ(relocated.paths().resolve_asset_path(relocated.startup_scene()),
            copy / "assets/levels/main.scene");
    }

    TEST_F(ProjectTest, OptionalSceneDoesNotFallBackToSampleAssets) {
        write("version: 1\nname: Empty Game\n");
        const auto project = Project::load(root);
        EXPECT_TRUE(project.startup_scene().empty());
        EXPECT_TRUE(std::filesystem::is_empty(root / "assets"));
    }

    TEST_F(ProjectTest, RejectsInvalidManifestAndDoesNotRewriteIt) {
        const std::string invalid[]{"[]", "version: 2\nname: Game",
            "version: 1\nname: ''", "version: 1\nname: Game\nextra: 1",
            "version: 1\nname: Game\nname: Duplicate",
            "version: 1\nname: Game\nstartup_scene: ../outside.scene",
            "version: 1\nname: Game\nstartup_scene: /outside.scene",
            "version: 1\nname: Game\nstartup_scene: texture.png",
            "version: 1\nname: Game\ndefault_material: 123"};
        for(const auto& contents : invalid) {
            SCOPED_TRACE(contents);
            write(contents);
            EXPECT_THROW(static_cast<void>(Project::load(root)), std::runtime_error);
            EXPECT_EQ(read_text_file(root / "project.yaml"), contents);
        }
    }

    TEST_F(ProjectTest, MissingProjectOrAssetsFailsWithoutCreatingThem) {
        EXPECT_THROW(
            static_cast<void>(Project::load(root)), std::filesystem::filesystem_error);
        EXPECT_FALSE(std::filesystem::exists(root / "project.yaml"));
        EXPECT_THROW(static_cast<void>(Project::load({})), std::runtime_error);
        write("version: 1\nname: Game\n");
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
        write("version: 1\nname: Game\nstartup_scene: link/main.scene\n");
        EXPECT_THROW(static_cast<void>(Project::load(root)), std::runtime_error);
    }
}
