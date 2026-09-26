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

    TEST_F(ProjectTest, SavesStartupSceneAtomicallyAndPreservesInputActions) {
        write(R"({"version":1,"name":"Game","startup_scene":"levels/old.scene",
            "input_actions":[{"name":"move","type":"axis","bindings":[
                {"source":"key","control":"L","scale":-1},
                {"source":"gamepad_axis","control":"LeftX","deadzone":0.15}]}]})");
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto project = std::move(loaded).value();
        ASSERT_TRUE(project.save_startup_scene("levels/new.scene"));
        EXPECT_EQ(project.startup_scene(), "levels/new.scene");

        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().startup_scene(), "levels/new.scene");
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::L, true);
        InputState frame;
        reopened.value().input_actions().evaluate(input.publish_frame(), frame);
        ASSERT_NE(frame.action("move"), nullptr);
        EXPECT_FLOAT_EQ(frame.action("move")->value, -1);
        const auto contents = read_text_file(root / "project.json");
        ASSERT_TRUE(contents) << contents.error();
        ASSERT_TRUE(project.save_startup_scene("levels/new.scene"));
        EXPECT_EQ(read_text_file(root / "project.json").value(), contents.value());

        ASSERT_TRUE(project.save_startup_scene({}));
        EXPECT_TRUE(project.startup_scene().empty());
        auto without_startup_scene = Project::load(root);
        ASSERT_TRUE(without_startup_scene) << without_startup_scene.error();
        EXPECT_TRUE(without_startup_scene.value().startup_scene().empty());
    }

    TEST_F(ProjectTest, FailedStartupSceneChangeRetainsOldProject) {
        write(R"({"version":1,"name":"Game","startup_scene":"levels/old.scene"})");
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto project = std::move(loaded).value();
        const auto original = read_text_file(root / "project.json").value();
        for(const auto& invalid : {"../outside.scene", "/outside.scene", "texture.png"}) {
            SCOPED_TRACE(invalid);
            EXPECT_FALSE(project.save_startup_scene(invalid));
            EXPECT_EQ(project.startup_scene(), "levels/old.scene");
            EXPECT_EQ(read_text_file(root / "project.json").value(), original);
        }

        const std::string external = R"({"version":1,"name":"Updated elsewhere"})";
        write(external);
        EXPECT_FALSE(project.save_startup_scene("levels/new.scene"));
        EXPECT_EQ(project.startup_scene(), "levels/old.scene");
        EXPECT_EQ(read_text_file(root / "project.json").value(), external);
    }

    TEST_F(ProjectTest, SavesNameWithoutLosingOtherSettings) {
        write(R"({"version":1,"name":"Old","startup_scene":"levels/main.scene",
            "input_actions":[{"name":"jump","type":"button","bindings":[]}]})");
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto project = std::move(loaded).value();
        ASSERT_TRUE(project.save_name("New Game"));
        EXPECT_EQ(project.name(), "New Game");
        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().name(), "New Game");
        EXPECT_EQ(reopened.value().startup_scene(), "levels/main.scene");
        EXPECT_EQ(reopened.value().input_actions().actions().size(), 1U);

        const auto saved = read_text_file(root / "project.json").value();
        for(const std::string invalid : {"", "  \t"}) {
            EXPECT_FALSE(project.save_name(invalid));
            EXPECT_EQ(project.name(), "New Game");
            EXPECT_EQ(read_text_file(root / "project.json").value(), saved);
        }
        write(R"({"version":1,"name":"External"})");
        EXPECT_FALSE(project.save_name("Stale"));
        EXPECT_EQ(project.name(), "New Game");
    }

    TEST_F(ProjectTest, SampleInputBindingsSurviveProjectSave) {
        const auto sample =
            read_text_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "project.json");
        ASSERT_TRUE(sample) << sample.error();
        write(sample.value());
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto project = std::move(loaded).value();
        ASSERT_TRUE(project.save_startup_scene("scenes/another.scene"));
        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().startup_scene(), "scenes/another.scene");
        EXPECT_EQ(reopened.value().input_actions().actions().size(),
            project.input_actions().actions().size());
    }

    TEST_F(ProjectTest, LoadsProjectActionsAndRejectsBadInputWithoutFallback) {
        write(R"({"version":1,"name":"Game","input_actions":[
            {"name":"move","type":"axis","bindings":[
                {"source":"key","control":"L","scale":-1}]},
            {"name":"disabled","type":"button","bindings":[]}]})");
        auto project = Project::load(root);
        ASSERT_TRUE(project) << project.error();
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::L, true);
        InputState frame;
        project.value().input_actions().evaluate(input.publish_frame(), frame);
        ASSERT_NE(frame.action("move"), nullptr);
        EXPECT_FLOAT_EQ(frame.action("move")->value, -1);
        EXPECT_FALSE(frame.action("disabled")->down);
        const std::string invalid[]{"null", "{}",
            R"([{"name":"move","type":"axis","bindings":[{"source":"key","control":"Typo"}]}])",
            R"([{"name":"move","type":"axis","bindings":[{"source":"key","control":"W","scale":null}]}])",
            R"([{"name":"move","type":"axis","bindings":[{"source":"gamepad_axis","control":"LeftX","deadzone":1}]}])",
            R"([{"name":"look","type":"delta","bindings":[{"source":"key","control":"W"}]}])",
            R"([{"name":"jump","type":"button","bindings":[],"extra":1}])",
            R"([{"name":"jump","type":"button","bindings":[]},{"name":"jump","type":"button","bindings":[]}])"};
        for(const auto& value : invalid) {
            SCOPED_TRACE(value);
            write("{\"version\":1,\"name\":\"Game\",\"input_actions\":" + value + "}");
            const auto loaded = Project::load(root);
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find("input_actions"), std::string::npos);
        }
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
        write(R"({"version":1,"name":"Game"})");
        std::filesystem::copy_file(root / "project.json", root / "alias.json");
        EXPECT_FALSE(Project::load(root / "alias.json"));
    }
}
