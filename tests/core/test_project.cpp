#include "core/project.h"
#include "asset/handle.h"
#include "common/file_io.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

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

        static Result<InputActions> persistent_actions(std::vector<InputActions::Action> actions) {
            for(auto& action : actions) {
                action.id = Uuid::generate();
                for(auto& binding : action.bindings)
                    binding.id = Uuid::generate();
            }
            return InputActions::create(std::move(actions));
        }

        static std::string manifest(std::string_view actions = "[]",
            std::string_view id = "11111111-1111-4111-8111-111111111111") {
            return R"({"version":2,"id":")" + std::string(id)
                   + R"(","name":"Game","startup_scene":"scenes/main.scene","input_actions":)"
                   + std::string(actions) + "}";
        }
    };

    TEST_F(ProjectTest, LoadsDirectoryOrManifestAndKeepsSettingsProjectRelative) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "My Game", "startup_scene": "levels/main.scene"})");
        auto project_result = Project::load(root);
        ASSERT_TRUE(project_result) << project_result.error();
        auto project = std::move(project_result).value();
        EXPECT_TRUE(project.id());
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
        EXPECT_EQ(relocated.id(), project.id());
        const auto relocated_scene =
            relocated.paths().resolve_asset_path(relocated.startup_scene());
        ASSERT_TRUE(relocated_scene) << relocated_scene.error();
        EXPECT_EQ(relocated_scene.value(), copy / "assets/levels/main.scene");
    }

    TEST_F(ProjectTest, StartupSceneIsRequired) {
        write(R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Empty Game"})");
        const auto missing = Project::load(root);
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().find("startup_scene"), std::string::npos);
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Empty Game", "startup_scene": ""})");
        const auto empty = Project::load(root);
        ASSERT_FALSE(empty);
        EXPECT_NE(empty.error().find("startup_scene"), std::string::npos);
        EXPECT_TRUE(std::filesystem::is_empty(root / "assets"));
    }

    TEST_F(ProjectTest, DisplayDefaultsSurviveOtherProjectEditsAndFailedSaves) {
        write(manifest());
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded);
        auto project = std::move(loaded).value();
        EXPECT_EQ(project.display_settings(), DisplaySettings{});
        const DisplaySettings defaults{
            1920, 1080, WindowMode::Borderless, true, {OutputMode::Auto, 6, 1.25f}};
        ASSERT_TRUE(project.save_display_settings(defaults));
        ASSERT_TRUE(project.save_name("Renamed"));
        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().display_settings(), defaults);
        EXPECT_FALSE(project.save_display_settings({0, 720}));
        std::filesystem::remove(root / "project.json");
        std::filesystem::create_directory(root / "project.json");
        EXPECT_FALSE(project.save_display_settings(DisplaySettings{}));
        EXPECT_EQ(project.display_settings(), defaults);
    }

    TEST_F(ProjectTest, QualityDefaultsSurviveDisplayAndNameEditsAndFailedSaves) {
        write(manifest());
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded);
        auto project = std::move(loaded).value();
        EXPECT_EQ(project.quality_settings(), QualitySettings{});
        const QualitySettings defaults{1, 2, 0.75f};
        ASSERT_TRUE(project.save_quality_settings(defaults));
        ASSERT_TRUE(project.save_display_settings({1280, 720}));
        ASSERT_TRUE(project.save_name("Renamed"));
        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().quality_settings(), defaults);
        const auto contents = read_text_file(root / "project.json");
        ASSERT_TRUE(contents);
        ASSERT_TRUE(project.save_quality_settings(defaults));
        EXPECT_EQ(read_text_file(root / "project.json").value(), contents.value());
        EXPECT_FALSE(project.save_quality_settings({3, 8, 1}));
        std::filesystem::remove(root / "project.json");
        std::filesystem::create_directory(root / "project.json");
        EXPECT_FALSE(project.save_quality_settings(QualitySettings{}));
        EXPECT_EQ(project.quality_settings(), defaults);
    }

    TEST_F(ProjectTest, AudioDefaultsPersistAlongsideDisplayQualityAndName) {
        write(manifest());
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded);
        auto project = std::move(loaded).value();
        EXPECT_EQ(project.audio_settings(), AudioSettings{});
        const AudioSettings defaults{0.7f, 0.6f, 0.5f};
        ASSERT_TRUE(project.save_audio_settings(defaults));
        ASSERT_TRUE(project.save_quality_settings({1, 2, 0.75f}));
        ASSERT_TRUE(project.save_display_settings({1280, 720}));
        ASSERT_TRUE(project.save_name("Renamed"));
        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().audio_settings(), defaults);
        EXPECT_EQ(reopened.value().quality_settings(), (QualitySettings{1, 2, 0.75f}));
        const auto contents = read_text_file(root / "project.json");
        ASSERT_TRUE(contents);
        ASSERT_TRUE(project.save_audio_settings(defaults));
        EXPECT_EQ(read_text_file(root / "project.json").value(), contents.value());
        EXPECT_FALSE(project.save_audio_settings({1, -1, 1}));
        std::filesystem::remove(root / "project.json");
        std::filesystem::create_directory(root / "project.json");
        EXPECT_FALSE(project.save_audio_settings(AudioSettings{}));
        EXPECT_EQ(project.audio_settings(), defaults);
    }

    TEST_F(ProjectTest, SavesStartupSceneAtomicallyAndPreservesInputActions) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"levels/old.scene",
            "input_actions":[{"id":"22222222-2222-4222-8222-000000000001","name":"move","type":"axis","bindings":[
                {"id":"22222222-2222-4222-8222-000000000017","source":"key","control":"L","scale":-1},
                {"id":"22222222-2222-4222-8222-000000000018","source":"gamepad_axis","control":"LeftX","deadzone":0.15}]}]})");
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

        EXPECT_FALSE(project.save_startup_scene({}));
        EXPECT_EQ(project.startup_scene(), "levels/new.scene");
        EXPECT_EQ(read_text_file(root / "project.json").value(), contents.value());
    }

    TEST_F(ProjectTest, FailedStartupSceneChangeRetainsOldProject) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"levels/old.scene"})");
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

        std::filesystem::rename(root / "project.json", root / "saved.json");
        ASSERT_TRUE(std::filesystem::create_directory(root / "project.json"));
        EXPECT_FALSE(project.save_startup_scene("levels/new.scene"));
        EXPECT_EQ(project.startup_scene(), "levels/old.scene");
        EXPECT_EQ(read_text_file(root / "saved.json").value(), original);
    }

    TEST_F(ProjectTest, SavesNameWithoutLosingOtherSettings) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Old","startup_scene":"levels/main.scene",
            "input_actions":[{"id":"22222222-2222-4222-8222-000000000002","name":"jump","type":"button","bindings":[]}]})");
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
    }

    TEST_F(ProjectTest, SavesInputActionsAtomically) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"levels/main.scene",
            "input_actions":[{"id":"22222222-2222-4222-8222-000000000003","name":"jump","type":"button","bindings":[]}]})");
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto project = std::move(loaded).value();
        auto right = InputActions::parse_binding("key", "Right");
        ASSERT_TRUE(right) << right.error();
        auto actions =
            persistent_actions({{"move", InputActions::Type::Axis, {std::move(right).value()}}});
        ASSERT_TRUE(actions) << actions.error();
        ASSERT_TRUE(project.save_input_actions(actions.value()));
        EXPECT_EQ(project.input_actions(), actions.value());
        const auto saved = read_text_file(root / "project.json");
        ASSERT_TRUE(saved) << saved.error();
        ASSERT_TRUE(project.save_input_actions(actions.value()));
        EXPECT_EQ(read_text_file(root / "project.json").value(), saved.value());

        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().name(), "Game");
        EXPECT_EQ(reopened.value().startup_scene(), "levels/main.scene");
        EXPECT_EQ(reopened.value().input_actions(), actions.value());
    }

    TEST_F(ProjectTest, PersistsExtendedKeyboardBindingsWithoutChangingTheirIdentity) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"scenes/main.scene"})");
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        const auto actions = persistent_actions({{"adjust", InputActions::Type::Axis,
            {{Input::Key::Keypad1, -1}, {Input::Key::Comma}, {Input::Key::KeypadEnter},
                {Input::Key::World1}, {Input::Key::World2}, {Input::Key::F25}}}});
        ASSERT_TRUE(actions) << actions.error();
        const auto saved = loaded.value().save_input_actions(actions.value());
        ASSERT_TRUE(saved) << saved.error();
        const auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().input_actions(), actions.value());
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Keypad1, true);
        InputState state;
        reopened.value().input_actions().evaluate(input.publish_frame(), state);
        ASSERT_NE(state.action("adjust"), nullptr);
        EXPECT_FLOAT_EQ(state.action("adjust")->value, -1);
    }

    TEST_F(ProjectTest, InputContextsAndActionMembershipSurviveAllProjectSettingsSaves) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"scenes/main.scene",
            "input_contexts":[{"name":"gameplay"},
                {"name":"camera","enabled":false,"priority":100,"consume":true}],
            "input_actions":[
                {"id":"22222222-2222-4222-8222-000000000004","name":"jump","type":"button","bindings":[],"context":"gameplay"},
                {"id":"22222222-2222-4222-8222-000000000005","name":"pause","type":"button","bindings":[]}]})");
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto project = std::move(loaded).value();
        const auto original = project.input_actions();
        ASSERT_EQ(original.contexts().size(), 2u);
        EXPECT_TRUE(original.contexts()[0].enabled);
        EXPECT_EQ(original.contexts()[0].priority, 0);
        EXPECT_FALSE(original.contexts()[0].consume);
        EXPECT_FALSE(original.contexts()[1].enabled);
        EXPECT_EQ(original.contexts()[1].priority, 100);
        EXPECT_TRUE(original.contexts()[1].consume);
        EXPECT_EQ(original.actions()[0].context, "gameplay");
        EXPECT_TRUE(original.actions()[1].context.empty());
        ASSERT_TRUE(project.save_name("Renamed"));
        ASSERT_TRUE(project.save_startup_scene("scenes/other.scene"));
        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().input_actions(), original);

        auto changed = InputActions::create(
            original.actions(), {{"gameplay", false, std::numeric_limits<int>::min(), true},
                                    {"camera", true, std::numeric_limits<int>::max(), false}});
        ASSERT_TRUE(changed) << changed.error();
        ASSERT_TRUE(project.save_input_actions(changed.value()));
        reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().input_actions(), changed.value());
    }

    TEST_F(ProjectTest, ContextsCanExistWithoutActionsAndInvalidDeclarationsAreRejected) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"scenes/main.scene",
            "input_contexts":[{"name":"gameplay"}]})");
        const auto project = Project::load(root);
        ASSERT_TRUE(project) << project.error();
        EXPECT_TRUE(project.value().input_actions().actions().empty());
        ASSERT_EQ(project.value().input_actions().contexts().size(), 1u);
        EXPECT_TRUE(project.value().input_actions().contexts()[0].enabled);

        for(const char* contexts : {"null", "{}", "[{}]", R"([{"name":42}])", R"([{"name":""}])",
                R"([{"name":"gameplay","enabled":1}])", R"([{"name":"gameplay","enabled":null}])",
                R"([{"name":"gameplay","priority":"high"}])",
                R"([{"name":"gameplay","priority":1.5}])",
                R"([{"name":"gameplay","priority":null}])",
                R"([{"name":"gameplay","priority":2147483648}])",
                R"([{"name":"gameplay","priority":-2147483649}])",
                R"([{"name":"gameplay","consume":1}])", R"([{"name":"gameplay","consume":null}])",
                R"([{"name":"gameplay","unknown":true}])",
                R"([{"name":"gameplay"},{"name":"gameplay"}])"}) {
            SCOPED_TRACE(contexts);
            write(
                std::string(
                    R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"scenes/main.scene","input_contexts":)")
                + contexts + "}");
            const auto loaded = Project::load(root);
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find("input_contexts"), std::string::npos);
        }
    }

    TEST_F(ProjectTest, ProjectAndInputIdentitiesSurviveRenameReorderingAndOtherSettingsSaves) {
        write(manifest());
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto& project = loaded.value();
        const auto project_id = project.id();
        const auto initial = persistent_actions(
            {{"move", InputActions::Type::Axis, {{Input::Key::D}, {Input::Key::A, -1}}},
                {"jump", InputActions::Type::Button, {{Input::Key::Space}}}});
        ASSERT_TRUE(initial) << initial.error();
        ASSERT_TRUE(project.save_input_actions(initial.value()));
        auto actions = initial.value().actions();
        actions[0].name = "movement";
        std::ranges::reverse(actions[0].bindings);
        std::ranges::reverse(actions);
        auto changed = InputActions::create(std::move(actions));
        ASSERT_TRUE(changed) << changed.error();
        ASSERT_TRUE(project.save_input_actions(changed.value()));
        ASSERT_TRUE(project.save_name("Renamed Game"));
        ASSERT_TRUE(project.save_startup_scene("scenes/other.scene"));

        const auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().id(), project_id);
        EXPECT_EQ(reopened.value().input_actions(), changed.value());
        const auto& saved = reopened.value().input_actions().actions();
        ASSERT_EQ(saved.size(), 2u);
        EXPECT_EQ(saved[1].name, "movement");
        EXPECT_EQ(saved[1].id, initial.value().actions()[0].id);
        ASSERT_EQ(saved[1].bindings.size(), 2u);
        EXPECT_EQ(saved[1].bindings[0].id, initial.value().actions()[0].bindings[1].id);
        EXPECT_EQ(saved[1].bindings[1].id, initial.value().actions()[0].bindings[0].id);
        EXPECT_EQ(saved[0].id, initial.value().actions()[1].id);
    }

    TEST_F(ProjectTest, IndependentProjectsKeepDifferentIdentitiesWithTheSameDisplayName) {
        write(manifest());
        const auto other = root / "other";
        std::filesystem::create_directories(other / "assets");
        ASSERT_TRUE(write_text_file_atomic(
            other / "project.json", manifest("[]", "33333333-3333-4333-8333-333333333333")));
        auto first = Project::load(root);
        auto second = Project::load(other);
        ASSERT_TRUE(first) << first.error();
        ASSERT_TRUE(second) << second.error();
        EXPECT_EQ(first.value().name(), second.value().name());
        EXPECT_NE(first.value().id(), second.value().id());
        const auto second_id = second.value().id();
        ASSERT_TRUE(first.value().save_name("Changed"));
        const auto reopened = Project::load(other);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().name(), "Game");
        EXPECT_EQ(reopened.value().id(), second_id);
    }

    TEST_F(ProjectTest, RejectsVersionOneWithoutRewritingOrCreatingMigrationFiles) {
        const std::string original = R"({"version":1,"name":"Legacy Game",
            "startup_scene":"scenes/main.scene","input_actions":[
                {"name":"jump","type":"button","bindings":[{"source":"key","control":"Space"}]}]})";
        write(original);
        const auto loaded = Project::load(root);
        ASSERT_FALSE(loaded);
        EXPECT_NE(loaded.error().find("unsupported version 1; expected 2"), std::string::npos);
        EXPECT_EQ(read_text_file(root / "project.json").value(), original);
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root),
                      std::filesystem::directory_iterator{}),
            2);
    }

    TEST_F(ProjectTest, RejectsMissingMalformedZeroAndDuplicatePersistentIdentities) {
        for(const std::string id : {"", "not-a-uuid", "00000000-0000-0000-0000-000000000000"}) {
            SCOPED_TRACE(id);
            const auto contents = manifest("[]", id);
            write(contents);
            const auto loaded = Project::load(root);
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find("id"), std::string::npos);
            EXPECT_EQ(read_text_file(root / "project.json").value(), contents);
        }
        write(R"({"version":2,"name":"Game","startup_scene":"scenes/main.scene"})");
        const auto missing_project_id = Project::load(root);
        ASSERT_FALSE(missing_project_id);
        EXPECT_NE(missing_project_id.error().find("id"), std::string::npos);

        constexpr std::string_view action_id = "44444444-4444-4444-8444-444444444444";
        constexpr std::string_view binding_id = "55555555-5555-4555-8555-555555555555";
        const auto action = [](std::string_view id, std::string_view bindings) {
            return R"({"id":")" + std::string(id) + R"(","name":"jump","type":"button","bindings":)"
                   + std::string(bindings) + "}";
        };
        const auto binding = [](std::string_view id) {
            return R"({"id":")" + std::string(id) + R"(","source":"key","control":"Space"})";
        };
        std::vector<std::string> invalid{R"([{"name":"jump","type":"button","bindings":[]}])",
            "[" + action(action_id, R"([{"source":"key","control":"Space"}])") + "]"};
        for(const std::string id : {"", "not-a-uuid", "00000000-0000-0000-0000-000000000000"}) {
            invalid.push_back("[" + action(id, "[]") + "]");
            invalid.push_back("[" + action(action_id, "[" + binding(id) + "]") + "]");
        }
        auto duplicate_action = action(action_id, "[]");
        duplicate_action.replace(duplicate_action.find("jump"), 4, "other");
        invalid.push_back("[" + action(action_id, "[]") + "," + duplicate_action + "]");
        invalid.push_back(
            "[" + action(action_id, "[" + binding(binding_id) + "," + binding(binding_id) + "]")
            + "]");
        for(const auto& actions : invalid) {
            SCOPED_TRACE(actions);
            const auto contents = manifest(actions);
            write(contents);
            const auto loaded = Project::load(root);
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find("input_actions"), std::string::npos);
            EXPECT_EQ(read_text_file(root / "project.json").value(), contents);
        }
    }

    TEST_F(ProjectTest, RejectsAnonymousSavedActionsWithoutChangingMemoryOrDisk) {
        write(manifest());
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        auto& project = loaded.value();
        auto original =
            persistent_actions({{"jump", InputActions::Type::Button, {{Input::Key::Space}}}});
        ASSERT_TRUE(original) << original.error();
        ASSERT_TRUE(project.save_input_actions(original.value()));
        const auto contents = read_text_file(root / "project.json").value();
        for(const bool remove_action_id : {true, false}) {
            auto definitions = original.value().actions();
            if(remove_action_id)
                definitions[0].id = {};
            else
                definitions[0].bindings[0].id = {};
            const auto anonymous = InputActions::create(std::move(definitions));
            ASSERT_TRUE(anonymous) << anonymous.error();
            EXPECT_FALSE(project.save_input_actions(anonymous.value()));
            EXPECT_EQ(project.input_actions(), original.value());
            EXPECT_EQ(read_text_file(root / "project.json").value(), contents);
        }
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
        EXPECT_EQ(reopened.value().input_actions(), project.input_actions());
    }

    TEST_F(ProjectTest, LoadsProjectActionsAndRejectsBadInputWithoutFallback) {
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game","startup_scene":"scenes/main.scene","input_actions":[
            {"id":"22222222-2222-4222-8222-000000000006","name":"move","type":"axis","bindings":[
                {"id":"22222222-2222-4222-8222-000000000019","source":"key","control":"L","scale":-1}]},
            {"id":"22222222-2222-4222-8222-000000000007","name":"disabled","type":"button","bindings":[]}]})");
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
            R"([{"id":"22222222-2222-4222-8222-000000000008","name":"move","type":"axis","bindings":[{"id":"22222222-2222-4222-8222-000000000020","source":"key","control":"Typo"}]}])",
            R"([{"id":"22222222-2222-4222-8222-000000000009","name":"move","type":"axis","bindings":[{"id":"22222222-2222-4222-8222-000000000021","source":"key","control":"W","scale":null}]}])",
            R"([{"id":"22222222-2222-4222-8222-000000000010","name":"move","type":"axis","bindings":[{"id":"22222222-2222-4222-8222-000000000022","source":"gamepad_axis","control":"LeftX","deadzone":1}]}])",
            R"([{"id":"22222222-2222-4222-8222-000000000011","name":"look","type":"delta","bindings":[{"id":"22222222-2222-4222-8222-000000000023","source":"key","control":"W"}]}])",
            R"([{"id":"22222222-2222-4222-8222-000000000012","name":"jump","type":"button","bindings":[],"context":1}])",
            R"([{"id":"22222222-2222-4222-8222-000000000013","name":"jump","type":"button","bindings":[],"context":"missing"}])",
            R"([{"id":"22222222-2222-4222-8222-000000000014","name":"jump","type":"button","bindings":[],"extra":1}])",
            R"([{"id":"22222222-2222-4222-8222-000000000015","name":"jump","type":"button","bindings":[]},{"id":"22222222-2222-4222-8222-000000000016","name":"jump","type":"button","bindings":[]}])"};
        for(const auto& value : invalid) {
            SCOPED_TRACE(value);
            write(
                "{\"version\":2,\"id\":\"11111111-1111-4111-8111-111111111111\",\"name\":\"Game\","
                "\"startup_scene\":\"scenes/main.scene\",\"input_actions\":"
                + value + "}");
            const auto loaded = Project::load(root);
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find("input_actions"), std::string::npos);
        }
    }

    TEST_F(ProjectTest, RejectsInvalidManifestAndDoesNotRewriteIt) {
        const std::string invalid[]{"[]", R"({"version": 3, "name": "Game"})",
            R"({"version": 0, "name": "Game"})", R"({"name": "Game"})",
            R"({"version": "1", "name": "Game"})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": ""})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": 42})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "extra": 1})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "name": "Duplicate"})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "startup_scene": "../outside.scene"})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "startup_scene": "/outside.scene"})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "startup_scene": "texture.png"})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "startup_scene": null})",
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "default_material": 123})",
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
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "startup_scene": "scenes/main.scene"})");
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
        write(
            R"({"version":2,"id":"11111111-1111-4111-8111-111111111111", "name": "Game", "startup_scene": "link/main.scene"})");
        EXPECT_FALSE(Project::load(root));
    }

    TEST_F(ProjectTest, DoesNotFallBackToLegacyManifest) {
        ASSERT_TRUE(write_text_file_atomic(root / "project.yaml", "version: 1\nname: Legacy\n"));
        EXPECT_FALSE(Project::load(root));
        EXPECT_FALSE(std::filesystem::exists(root / "project.json"));
        EXPECT_FALSE(Project::load(root / "project.yaml"));
        write(R"({"version":2,"id":"11111111-1111-4111-8111-111111111111","name":"Game"})");
        std::filesystem::copy_file(root / "project.json", root / "alias.json");
        EXPECT_FALSE(Project::load(root / "alias.json"));
    }
    TEST_F(ProjectTest, OptionalUiEntrySurvivesProjectSettingsSaves) {
        auto source = manifest();
        source.insert(
            source.size() - 1, R"(,"ui":{"document":"ui/menu.rml","controller":"ui/menu.ui.lua"})");
        write(source);
        auto loaded = Project::load(root);
        ASSERT_TRUE(loaded) << loaded.error();
        ASSERT_TRUE(loaded.value().ui());
        EXPECT_EQ(loaded.value().ui()->document, "ui/menu.rml");
        EXPECT_EQ(loaded.value().ui()->controller, "ui/menu.ui.lua");
        ASSERT_TRUE(loaded.value().save_name("Renamed"));
        ASSERT_TRUE(loaded.value().save_startup_scene("scenes/other.scene"));
        ASSERT_TRUE(loaded.value().save_input_actions(InputActions{}));
        auto reopened = Project::load(root);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().ui(), loaded.value().ui());
        write(manifest());
        auto without_ui = Project::load(root);
        ASSERT_TRUE(without_ui);
        EXPECT_FALSE(without_ui.value().ui());
    }

    TEST_F(ProjectTest, UiEntriesRejectMalformedAndEscapingPaths) {
        for(const char* entry : {"null", "{}", R"({"document":"ui/menu.rml"})",
                R"({"document":"../menu.rml","controller":"ui/menu.ui.lua"})",
                R"({"document":"ui/menu.rml","controller":"/menu.ui.lua"})",
                R"({"document":"ui/menu.rml","controller":"ui/menu.lua"})",
                R"({"document":"ui/menu.rcss","controller":"ui/menu.ui.lua"})",
                R"({"document":"ui/menu.rml","controller":"ui/menu.ui.lua","extra":true})"}) {
            SCOPED_TRACE(entry);
            auto source = manifest();
            source.insert(source.size() - 1, ",\"ui\":" + std::string(entry));
            write(source);
            EXPECT_FALSE(Project::load(root));
        }
        auto source = manifest();
        source.insert(source.size() - 1,
            R"(,"ui":{"document":"ui/alias/menu.rml","controller":"ui/menu.ui.lua"})");
        std::filesystem::create_directories(root / "assets/ui");
        std::filesystem::create_directory_symlink(root, root / "assets/ui/alias");
        write(source);
        EXPECT_FALSE(Project::load(root));
    }

}
