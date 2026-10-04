#include "input/player_input_settings.h"

#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Comet::Tests {
    namespace {
        class EnvironmentScope {
        public:
            explicit EnvironmentScope(const char* key) : m_key(key) {
                if(const char* previous = std::getenv(key))
                    m_previous = previous;
            }
            ~EnvironmentScope() { set(m_previous); }
            EnvironmentScope(const EnvironmentScope&) = delete;
            EnvironmentScope& operator=(const EnvironmentScope&) = delete;

            void set(const std::optional<std::string>& value) const {
#ifdef _WIN32
                EXPECT_EQ(_putenv_s(m_key, value ? value->c_str() : ""), 0);
#else
                EXPECT_EQ(value ? setenv(m_key, value->c_str(), 1) : unsetenv(m_key), 0);
#endif
            }

        private:
            const char* m_key;
            std::optional<std::string> m_previous;
        };
    }

    class PlayerInputSettingsTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        const std::filesystem::path file = directory.path() / "settings/input.json";
        const Uuid project_id = Uuid::generate();
        const Uuid action_id = Uuid::generate();
        const Uuid binding_id = Uuid::generate();

        Result<InputOverrides> remapping(Input::Key key = Input::Key::J) const {
            return InputOverrides::create({{action_id, InputActions::Type::Button, false,
                {{.id = binding_id, .control = key}}}});
        }

        std::string document(std::string_view actions = "[]") const {
            return R"({"version":2,"project_id":")" + project_id.to_string() + R"(","actions":)"
                   + std::string(actions) + "}";
        }

        std::string action(std::string_view fields) const {
            return R"({"id":")" + action_id.to_string() + R"(","type":"axis",)"
                   + std::string(fields) + "}";
        }

        std::string binding(std::string_view fields) const {
            return R"({"id":")" + binding_id.to_string() + "\"," + std::string(fields) + "}";
        }

        void write(std::string_view contents) const {
            const auto saved = write_text_file_atomic(file, contents);
            ASSERT_TRUE(saved) << saved.error();
        }
    };

    TEST_F(PlayerInputSettingsTest, MissingFileAndUnchangedSaveDoNotCreateDirectories) {
        auto loaded = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().path(), file);
        EXPECT_TRUE(loaded.value().overrides().actions().empty());
        EXPECT_TRUE(loaded.value().save(InputOverrides{}));
        EXPECT_FALSE(std::filesystem::exists(file.parent_path()));
        EXPECT_TRUE(std::filesystem::is_empty(directory.path()));
    }

    TEST_F(PlayerInputSettingsTest, RoundTripsPartialFieldsDisabledRecordsAndAllControlSources) {
        const auto candidate = InputOverrides::create(
            {{action_id, InputActions::Type::Axis, false,
                 {{.id = binding_id, .control = Input::Key::World1},
                     {.id = Uuid::generate(), .control = Input::MouseButton::Extra5},
                     {.id = Uuid::generate(), .control = Input::GamepadButton::North},
                     {.id = Uuid::generate(),
                         .control = Input::GamepadAxis::RightX,
                         .deadzone = 0.25f},
                     {.id = Uuid::generate(), .scale = -2.0f},
                     {.id = Uuid::generate(), .deadzone = 0.0f},
                     {.id = Uuid::generate(), .disabled = true}}},
                {Uuid::generate(), InputActions::Type::Button, true, {}},
                {Uuid::generate(), InputActions::Type::Delta, false,
                    {{.id = Uuid::generate(),
                        .control = InputActions::Motion::CursorY,
                        .scale = -1.0f}}}});
        ASSERT_TRUE(candidate) << candidate.error();
        auto loaded = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(loaded) << loaded.error();
        ASSERT_TRUE(loaded.value().save(candidate.value()));
        EXPECT_EQ(loaded.value().overrides(), candidate.value());
        const auto reopened = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().overrides(), candidate.value());
        const auto source = read_text_file(file);
        ASSERT_TRUE(source) << source.error();
        EXPECT_NE(source.value().find("World1"), std::string::npos);
        EXPECT_NE(source.value().find("CursorY"), std::string::npos);
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(file.parent_path()),
                      std::filesystem::directory_iterator{}),
            1);
    }

    TEST_F(PlayerInputSettingsTest, SameOverridesKeepOriginalFormattingAndTimestamp) {
        const auto original =
            document("["
                     + action("\"bindings\":[" + binding(R"("disabled":false,"scale":-1)")
                              + "],\"disabled\":false")
                     + "]")
            + "\n\n";
        write(original);
        auto loaded = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(loaded) << loaded.error();
        const auto timestamp = std::filesystem::last_write_time(file);
        ASSERT_TRUE(loaded.value().save(loaded.value().overrides()));
        EXPECT_EQ(read_text_file(file).value(), original);
        EXPECT_EQ(std::filesystem::last_write_time(file), timestamp);
    }

    TEST_F(PlayerInputSettingsTest, DisabledFlagsRoundTripAllRetainedFieldsAndBindings) {
        const auto axis_binding_id = Uuid::generate();
        const auto enabled = InputOverrides::create({{action_id, InputActions::Type::Axis, false,
            {{.id = binding_id, .control = Input::Key::K, .scale = -2, .deadzone = 0},
                {.id = axis_binding_id,
                    .control = Input::GamepadAxis::RightX,
                    .scale = 0.5f,
                    .deadzone = 0.3f}}}});
        ASSERT_TRUE(enabled);
        for(const bool action_disabled : {false, true}) {
            for(const bool binding_disabled : {false, true}) {
                SCOPED_TRACE(testing::Message()
                             << "action=" << action_disabled << ", binding=" << binding_disabled);
                auto records = enabled.value().actions();
                records[0].disabled = action_disabled;
                for(auto& binding : records[0].bindings)
                    binding.disabled = binding_disabled;
                const auto candidate = InputOverrides::create(std::move(records));
                ASSERT_TRUE(candidate);
                auto loaded = PlayerInputSettings::load(project_id, file);
                ASSERT_TRUE(loaded) << loaded.error();
                ASSERT_TRUE(loaded.value().save(candidate.value()));
                auto reopened = PlayerInputSettings::load(project_id, file);
                ASSERT_TRUE(reopened) << reopened.error();
                EXPECT_EQ(reopened.value().overrides(), candidate.value());

                auto restored = reopened.value().overrides().actions();
                restored[0].disabled = false;
                for(auto& binding : restored[0].bindings)
                    binding.disabled = false;
                const auto reenabled = InputOverrides::create(std::move(restored));
                ASSERT_TRUE(reenabled);
                EXPECT_EQ(reenabled.value(), enabled.value());
                ASSERT_TRUE(reopened.value().save(reenabled.value()));
                const auto active = PlayerInputSettings::load(project_id, file);
                ASSERT_TRUE(active);
                EXPECT_EQ(active.value().overrides(), enabled.value());
            }
        }
    }

    TEST_F(PlayerInputSettingsTest, KeepsIncompatibleFiniteValuesForDefaultDependentResolution) {
        const auto candidate =
            InputOverrides::create({{action_id, InputActions::Type::Button, false,
                {{.id = binding_id,
                    .control = Input::GamepadAxis::RightX,
                    .scale = 101.0f,
                    .deadzone = 2.0f}}}});
        ASSERT_TRUE(candidate) << candidate.error();
        auto loaded = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(loaded.value().save(candidate.value()));
        const auto reopened = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value().overrides(), candidate.value());
    }

    TEST_F(
        PlayerInputSettingsTest, RestoringAllDefaultsWritesEmptyActionsWithoutDeletingOtherFiles) {
        auto loaded = PlayerInputSettings::load(project_id, file);
        const auto candidate = remapping();
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(candidate);
        ASSERT_TRUE(loaded.value().save(candidate.value()));
        const auto other = file.parent_path() / "other.json";
        ASSERT_TRUE(write_text_file_atomic(other, "keep"));
        ASSERT_TRUE(loaded.value().save(InputOverrides{}));
        EXPECT_TRUE(std::filesystem::is_regular_file(file));
        const auto reopened = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_TRUE(reopened.value().overrides().actions().empty());
        EXPECT_EQ(read_text_file(other).value(), "keep");
    }

    TEST_F(PlayerInputSettingsTest, ExternalEditsConflictEvenWhenSavingUnchangedOverrides) {
        auto loaded = PlayerInputSettings::load(project_id, file);
        const auto candidate = remapping();
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(candidate);
        ASSERT_TRUE(loaded.value().save(candidate.value()));
        const auto original = read_text_file(file).value();
        const auto external = original + "\n";
        write(external);
        EXPECT_FALSE(loaded.value().save(candidate.value()));
        EXPECT_FALSE(loaded.value().save(InputOverrides{}));
        EXPECT_EQ(loaded.value().overrides(), candidate.value());
        EXPECT_EQ(read_text_file(file).value(), external);
        write(original);
        EXPECT_TRUE(loaded.value().save(InputOverrides{}));
    }

    TEST_F(PlayerInputSettingsTest, ExternallyCreatedOrRemovedFileDoesNotReplaceLoadedBaseline) {
        auto missing = PlayerInputSettings::load(project_id, file);
        const auto candidate = remapping();
        ASSERT_TRUE(missing);
        ASSERT_TRUE(candidate);
        const auto external = document();
        write(external);
        EXPECT_FALSE(missing.value().save(candidate.value()));
        EXPECT_FALSE(missing.value().save(InputOverrides{}));
        EXPECT_TRUE(missing.value().overrides().actions().empty());
        EXPECT_EQ(read_text_file(file).value(), external);
        auto existing = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(existing);
        ASSERT_TRUE(std::filesystem::remove(file));
        EXPECT_FALSE(existing.value().save(candidate.value()));
        EXPECT_TRUE(existing.value().overrides().actions().empty());
        EXPECT_FALSE(std::filesystem::exists(file));
        EXPECT_TRUE(missing.value().save(candidate.value()));
    }

    TEST_F(PlayerInputSettingsTest, FilesystemFailurePreservesMemoryAndAllowsRetry) {
        auto loaded = PlayerInputSettings::load(project_id, file);
        const auto candidate = remapping();
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(candidate);
        ASSERT_TRUE(write_text_file_atomic(file.parent_path(), "blocking regular file"));
        EXPECT_FALSE(loaded.value().save(candidate.value()));
        EXPECT_TRUE(loaded.value().overrides().actions().empty());
        EXPECT_EQ(read_text_file(file.parent_path()).value(), "blocking regular file");
        ASSERT_TRUE(std::filesystem::remove(file.parent_path()));
        EXPECT_TRUE(loaded.value().save(candidate.value()));
        EXPECT_EQ(loaded.value().overrides(), candidate.value());
    }

    TEST_F(PlayerInputSettingsTest, RejectsInvalidTopLevelAndWrongProjectWithoutRewriting) {
        const auto correct = document();
        const std::vector<std::string> invalid{"", "{", "[]", "null",
            R"({"version":3,"project_id":")" + project_id.to_string() + R"(","actions":[]})",
            R"({"version":2,"actions":[]})", R"({"version":2,"project_id":null,"actions":[]})",
            R"({"version":2,"project_id":"bad","actions":[]})",
            R"({"version":2,"project_id":"00000000-0000-0000-0000-000000000000","actions":[]})",
            R"({"version":2,"project_id":")" + Uuid::generate().to_string() + R"(","actions":[]})",
            R"({"project_id":")" + project_id.to_string() + R"(","actions":[]})",
            correct.substr(0, correct.size() - 1) + R"(,"extra":1})",
            correct.substr(0, correct.size() - 1) + R"(,"version":2})", document("null"),
            document("{}")};
        for(const auto& contents : invalid) {
            SCOPED_TRACE(contents);
            write(contents);
            EXPECT_FALSE(PlayerInputSettings::load(project_id, file));
            EXPECT_EQ(read_text_file(file).value(), contents);
        }
    }

    TEST_F(PlayerInputSettingsTest, RejectsVersionOneWithoutRewritingOrMigrating) {
        const auto original = R"({ "version":1,"project_id":")" + project_id.to_string()
                              + R"(","actions":[)" + action(R"("disabled":true)") + "] }\n";
        write(original);
        const auto timestamp = std::filesystem::last_write_time(file);
        const auto loaded = PlayerInputSettings::load(project_id, file);
        ASSERT_FALSE(loaded);
        EXPECT_NE(loaded.error().find("unsupported version"), std::string::npos);
        EXPECT_EQ(read_text_file(file).value(), original);
        EXPECT_EQ(std::filesystem::last_write_time(file), timestamp);
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(file.parent_path()),
                      std::filesystem::directory_iterator{}),
            1);
    }

    TEST_F(PlayerInputSettingsTest, RejectsInvalidOverrideSchemaWithoutPartialLoad) {
        const auto binding_array = [&](std::string_view fields) {
            return "[" + action("\"bindings\":[" + binding(fields) + "]") + "]";
        };
        std::vector<std::string> invalid{"[null]", "[{}]", "[" + action(R"("bindings":null)") + "]",
            "[" + action(R"("bindings":[])") + "]", "[" + action(R"("disabled":false)") + "]",
            "[" + action(R"("disabled":1)") + "]",
            "[" + action(R"("disabled":true,"extra":1)") + "]",
            "[" + action(R"("disabled":true,"disabled":true)") + "]",
            "[" + action(R"("disabled":true)") + "," + action(R"("disabled":true)") + "]",
            "[" + action("\"disabled\":true,\"bindings\":[" + binding(R"("source":"key")") + "]")
                + "]",
            binding_array(R"("disabled":false)"), binding_array(R"("source":"key")"),
            binding_array(R"("control":"J")"), binding_array(R"("source":1,"control":"J")"),
            binding_array(R"("source":"key","control":null)"),
            binding_array(R"("source":"key","control":"UnknownKey")"),
            binding_array(R"("source":"unknown","control":"J")"), binding_array(R"("scale":null)"),
            binding_array(R"("scale":1e100)"), binding_array(R"("deadzone":"0.1")"),
            binding_array(R"("disabled":true,"scale":1e100)"),
            binding_array(R"("disabled":true,"source":"key","control":"UnknownKey")"),
            binding_array(R"("scale":1,"extra":1)"), binding_array(R"("scale":1,"scale":2)")};
        const auto duplicate = binding(R"("scale":1)");
        invalid.push_back("[" + action("\"bindings\":[" + duplicate + "," + duplicate + "]") + "]");
        invalid.push_back(
            "[" + action("\"disabled\":true,\"bindings\":[" + duplicate + "," + duplicate + "]")
            + "]");
        invalid.push_back("[" + action(R"("bindings":[{"scale":1}])") + "]");
        for(const std::string id : {"", "bad", "00000000-0000-0000-0000-000000000000"}) {
            invalid.push_back(R"([{"id":")" + id + R"(","type":"button","disabled":true}])");
            invalid.push_back(
                "[" + action(R"("bindings":[{"id":")" + id + R"(","scale":1}])") + "]");
        }
        auto bad_type = action(R"("disabled":true)");
        bad_type.replace(bad_type.find("axis"), 4, "unknown");
        invalid.push_back("[" + bad_type + "]");
        for(const auto& actions : invalid) {
            SCOPED_TRACE(actions);
            const auto contents = document(actions);
            write(contents);
            EXPECT_FALSE(PlayerInputSettings::load(project_id, file));
            EXPECT_EQ(read_text_file(file).value(), contents);
        }
    }

    TEST_F(PlayerInputSettingsTest, RejectsExcessiveActionOrBindingRecords) {
        std::string actions = "[";
        for(std::size_t index = 0; index <= InputActions::MAX_ACTIONS; ++index) {
            if(index)
                actions += ',';
            actions += R"({"id":")" + Uuid::generate().to_string()
                       + R"(","type":"button","disabled":true})";
        }
        actions += ']';
        write(document(actions));
        EXPECT_FALSE(PlayerInputSettings::load(project_id, file));
        EXPECT_EQ(read_text_file(file).value(), document(actions));

        std::string bindings = "[";
        for(std::size_t index = 0; index <= InputActions::MAX_BINDINGS; ++index) {
            if(index)
                bindings += ',';
            bindings += R"({"id":")" + Uuid::generate().to_string() + R"(","scale":1})";
        }
        bindings += ']';
        const auto contents = document("[" + action("\"bindings\":" + bindings) + "]");
        write(contents);
        EXPECT_FALSE(PlayerInputSettings::load(project_id, file));
        EXPECT_EQ(read_text_file(file).value(), contents);
    }

    TEST_F(PlayerInputSettingsTest, DifferentProjectsCannotLoadEachOthersExplicitFile) {
        const auto candidate = remapping();
        auto loaded = PlayerInputSettings::load(project_id, file);
        ASSERT_TRUE(candidate);
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(loaded.value().save(candidate.value()));
        const auto contents = read_text_file(file).value();
        EXPECT_FALSE(PlayerInputSettings::load(Uuid::generate(), file));
        EXPECT_EQ(read_text_file(file).value(), contents);
    }

    TEST_F(PlayerInputSettingsTest, RejectsEmptyPathsZeroProjectAndDirectories) {
        EXPECT_FALSE(PlayerInputSettings::load(project_id, {}));
        EXPECT_FALSE(PlayerInputSettings::load({}, file));
        EXPECT_FALSE(PlayerInputSettings::default_path({}));
        EXPECT_FALSE(PlayerInputSettings::load(project_id, directory.path()));
        EXPECT_TRUE(std::filesystem::is_empty(directory.path()));
    }

    TEST_F(PlayerInputSettingsTest, RelativeExplicitPathIsFixedAtLoadTime) {
        const auto relative = std::filesystem::relative(file);
        auto loaded = PlayerInputSettings::load(project_id, relative);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_TRUE(loaded.value().path().is_absolute());
        EXPECT_EQ(loaded.value().path(), std::filesystem::absolute(relative));
        const auto candidate = remapping();
        ASSERT_TRUE(candidate);
        ASSERT_TRUE(loaded.value().save(candidate.value()));
        EXPECT_TRUE(std::filesystem::is_regular_file(file));
    }

    TEST_F(PlayerInputSettingsTest, DefaultPathSeparatesProjectsAndLoadDoesNotCreateFiles) {
#ifdef _WIN32
        EnvironmentScope root("APPDATA");
        root.set(directory.path().string());
        const auto base = directory.path() / "Comet";
#elif defined(__APPLE__)
        EnvironmentScope root("HOME");
        root.set(directory.path().string());
        const auto base = directory.path() / "Library/Application Support/Comet";
#else
        EnvironmentScope root("XDG_CONFIG_HOME");
        root.set(directory.path().string());
        const auto base = directory.path() / "comet";
#endif
        const auto first = PlayerInputSettings::default_path(project_id);
        const auto other_id = Uuid::generate();
        const auto second = PlayerInputSettings::default_path(other_id);
        ASSERT_TRUE(first) << first.error();
        ASSERT_TRUE(second) << second.error();
        EXPECT_EQ(first.value(), base / "players" / project_id.to_string() / "default/input.json");
        EXPECT_EQ(second.value(), base / "players" / other_id.to_string() / "default/input.json");
        EXPECT_NE(first.value(), second.value());
        const auto loaded = PlayerInputSettings::load(project_id);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value().path(), first.value());
        EXPECT_TRUE(std::filesystem::is_empty(directory.path()));
    }

    TEST_F(PlayerInputSettingsTest, MissingOrRelativeUserRootsFailWithoutCreatingDirectories) {
#ifdef _WIN32
        EnvironmentScope root("APPDATA");
#elif defined(__APPLE__)
        EnvironmentScope root("HOME");
#else
        EnvironmentScope config("XDG_CONFIG_HOME");
        config.set(std::nullopt);
        EnvironmentScope root("HOME");
#endif
        for(const std::optional<std::string> value : {std::optional<std::string>{},
                std::optional<std::string>{""}, std::optional<std::string>{"relative"}}) {
            root.set(value);
            EXPECT_FALSE(PlayerInputSettings::default_path(project_id));
            EXPECT_FALSE(PlayerInputSettings::load(project_id));
        }
        EXPECT_TRUE(std::filesystem::is_empty(directory.path()));
    }

#if !defined(_WIN32) && !defined(__APPLE__)
    TEST_F(PlayerInputSettingsTest, LinuxUsesHomeOnlyWhenXdgConfigIsUnsetOrEmpty) {
        EnvironmentScope config("XDG_CONFIG_HOME");
        EnvironmentScope root("HOME");
        root.set(directory.path().string());
        for(const std::optional<std::string> value :
            {std::optional<std::string>{}, std::optional<std::string>{""}}) {
            config.set(value);
            const auto path = PlayerInputSettings::default_path(project_id);
            ASSERT_TRUE(path) << path.error();
            EXPECT_EQ(path.value(), directory.path() / ".config/comet/players"
                                        / project_id.to_string() / "default/input.json");
        }
        config.set("relative");
        EXPECT_FALSE(PlayerInputSettings::default_path(project_id));
        EXPECT_TRUE(std::filesystem::is_empty(directory.path()));
    }
#endif
}
