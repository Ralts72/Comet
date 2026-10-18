#include "ui/project_ui.h"
#include "config/display_settings_preview.h"
#include "support/engine_fixture.h"
#include "support/temporary_directory.h"
#include "core/window.h"
#include "render/overlay_record_context.h"
#include "common/scope_exit.h"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <string>
#include <utility>

namespace Comet::Tests {
    class ProjectUiGpuTest: public EngineTest {
    protected:
        TemporaryDirectory documents;
        std::unique_ptr<Ui::ProjectUi> ui;
        std::string original_document, original_controller;
        InputOverrides saved;
        std::vector<InputOverrides> submitted;
        bool fail_save = false;
        unsigned loads = 0;
        bool input_blocked = false;
        std::function<void(GLFWwindow*)> pending_input;
        bool display_services = false;
        bool fail_display_save = false;
        bool preview = false;
        std::optional<bool> vsync_active;
        DisplaySettings saved_display;
        unsigned display_applications = 0;
        bool display_confirmation_services = false;
        Uuid display_project_id = Uuid::generate();
        std::optional<PlayerDisplaySettings> display_player;
        std::unique_ptr<DisplaySettingsPreview> display_confirmation;
        DisplaySettings active_display;
        DisplaySettingsPreview::Clock::time_point display_time{};
        bool quality_services = false, fail_quality_save = false;
        QualitySettings saved_quality;
        QualitySettings default_quality;
        unsigned quality_applications = 0;
        bool audio_services = false, fail_audio_save = false;
        AudioSettings saved_audio;
        unsigned audio_applications = 0;
        ParameterMap session_values;
        std::vector<std::string> game_events;
        std::vector<std::optional<ParameterValue>> game_event_values;
        std::string settings_load_error;

        void SetUp() override {
            EngineTest::SetUp();
            ASSERT_TRUE(engine);
            std::filesystem::create_directories(documents.path() / "ui");
            for(const auto* name : {"runtime.rml", "runtime.rcss", "runtime.ui.lua"})
                std::filesystem::copy_file(std::filesystem::path(COMET_TEST_UI_DIRECTORY) / name,
                    documents.path() / "ui" / name);
            original_document = read("runtime.rml");
            original_controller = read("runtime.ui.lua");
            const auto project = Project::load(std::filesystem::path(PROJECT_ROOT_DIR) / "demo");
            ASSERT_TRUE(project) << project.error();
            create_ui(project.value().input_actions());
        }
        void create_ui(const InputActions& actions,
            Project::UiEntry entry = {"ui/runtime.rml", "ui/runtime.ui.lua"}) {
            engine->get_renderer().set_overlay({});
            engine->get_renderer().wait_idle();
            ui.reset();
            Ui::ProjectUi::Services services;
            services.input_actions = actions;
            services.session_get = [this](std::string_view key) -> std::optional<ParameterValue> {
                const auto found = session_values.find(std::string(key));
                if(found == session_values.end())
                    return std::nullopt;
                return found->second;
            };
            services.emit = [this](std::string_view event, std::optional<ParameterValue> value) {
                game_events.emplace_back(event);
                game_event_values.push_back(std::move(value));
                return true;
            };
            services.load_input = [this] {
                ++loads;
                return Result<InputOverrides>::success(saved);
            };
            services.apply_input = [this](InputOverrides value) {
                submitted.push_back(value);
                if(fail_save)
                    return Result<void>::failure("Simulated save failure");
                saved = std::move(value);
                return Result<void>::success();
            };
            if(display_services) {
                services.display_defaults = {1280, 720, WindowMode::Windowed, true};
                services.load_display = [this] {
                    if(!settings_load_error.empty())
                        return Result<DisplaySettings>::failure(settings_load_error);
                    if(display_confirmation)
                        return Result<DisplaySettings>::success(display_confirmation->settings());
                    return Result<DisplaySettings>::success(saved_display);
                };
                services.apply_display = [this](DisplaySettings value) {
                    if(display_confirmation)
                        return display_confirmation->apply(
                            value, active_display,
                            [this](const auto& settings) { return apply_display(settings); },
                            display_time);
                    if(fail_display_save)
                        return Result<void>::failure("Simulated display save failure");
                    if(auto result = apply_display(value); !result)
                        return result;
                    saved_display = value;
                    return Result<void>::success();
                };
                if(display_confirmation_services) {
                    display_confirmation.reset();
                    auto player = PlayerDisplaySettings::load(
                        display_project_id, saved_display, documents.path() / "display.json");
                    ASSERT_TRUE(player) << player.error();
                    display_player.emplace(std::move(player).value());
                    display_confirmation =
                        std::make_unique<DisplaySettingsPreview>(*display_player);
                    active_display = saved_display;
                    services.display_confirmation = [this] {
                        return display_confirmation->remaining(display_time);
                    };
                    services.confirm_display = [this] {
                        if(engine->get_renderer().output_pending())
                            return Result<void>::failure("Display output is still being applied");
                        return display_confirmation->confirm(display_time);
                    };
                    services.revert_display = [this] {
                        return display_confirmation->revert(
                            [this](const auto& settings) { return apply_display(settings); });
                    };
                }
            }
            if(quality_services) {
                services.quality_defaults = default_quality;
                services.load_quality = [this] {
                    if(!settings_load_error.empty())
                        return Result<QualitySettings>::failure(settings_load_error);
                    return Result<QualitySettings>::success(saved_quality);
                };
                services.apply_quality = [this](QualitySettings value) {
                    if(fail_quality_save)
                        return Result<void>::failure("Simulated quality save failure");
                    const auto requested = engine->get_renderer().request_quality_settings(value);
                    if(!requested)
                        return Result<void>::failure(requested.error().message);
                    saved_quality = value;
                    ++quality_applications;
                    return Result<void>::success();
                };
            }
            if(audio_services) {
                services.audio_defaults = {0.7f, 0.6f, 0.5f};
                services.load_audio = [this] {
                    if(!settings_load_error.empty())
                        return Result<AudioSettings>::failure(settings_load_error);
                    return Result<AudioSettings>::success(saved_audio);
                };
                services.active_audio = [this] { return engine->get_audio_settings(); };
                services.apply_audio = [this](AudioSettings value) {
                    if(fail_audio_save)
                        return Result<void>::failure("Simulated audio save failure");
                    const auto applied = engine->apply_audio_settings(value);
                    if(!applied)
                        return Result<void>::failure(applied.error().message);
                    saved_audio = value;
                    ++audio_applications;
                    return Result<void>::success();
                };
            }
            auto created = Ui::ProjectUi::create(engine->get_window(), engine->get_renderer(),
                entry,
                {.resource_root = documents.path(), .font_directory = COMET_TEST_UI_FONT_DIRECTORY},
                std::move(services));
            ASSERT_TRUE(created) << created.error();
            ui = std::move(created).value();
            engine->get_renderer().set_overlay({.capture =
                                                    [this] {
                                                        OverlayDrawList draws;
                                                        if(auto frame = ui->take_draw_frame())
                                                            draws.push_back(std::move(frame));
                                                        return draws;
                                                    },
                .release = [this] { ui->release_swapchain_resources(); },
                .rebuild =
                    [this](const SwapchainCompatibility& compatibility) {
                        return ui->rebuild_swapchain_resources(compatibility);
                    }});
            const auto result = submit_frame();
            ASSERT_TRUE(result) << result.error().message;
        }
        void TearDown() override {
            if(engine) {
                engine->get_renderer().set_overlay({});
                engine->get_renderer().wait_idle();
            }
            ui.reset();
            EngineTest::TearDown();
        }
        std::string read(const char* name) {
            std::ifstream file(documents.path() / "ui" / name);
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }
        void write(const char* name, const std::string& source) {
            std::ofstream file(documents.path() / "ui" / name, std::ios::trunc);
            ASSERT_TRUE(file);
            file << source;
            file.flush();
            ASSERT_TRUE(file);
        }
        Rml::ElementDocument& document() { return *Rml::GetContext(0)->GetDocument(0); }
        void click(const std::string& id) {
            auto* element = document().GetElementById(id);
            ASSERT_NE(element, nullptr) << id;
            element->Click();
            const auto result = submit_frame();
            ASSERT_TRUE(result) << result.error().message;
        }
        Result<void, GraphicsError> submit_frame() {
            if(display_confirmation) {
                auto expired = display_confirmation->expire(
                    [this](const auto& settings) { return apply_display(settings); }, display_time);
                if(!expired)
                    return Result<void, GraphicsError>::failure({expired.error()});
            }
            auto& window = engine->get_window();
            auto& renderer = engine->get_renderer();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                window.poll_events();
                if(pending_input) {
                    // 在 poll 后交付模拟输入，避免系统焦点事件打断测试点击。
                    const auto focus = glfwSetWindowFocusCallback(window.get(), nullptr);
                    glfwSetWindowFocusCallback(window.get(), focus);
                    if(!focus)
                        return Result<void, GraphicsError>::failure({"Missing focus callback"});
                    focus(window.get(), GLFW_TRUE);
                    std::exchange(pending_input, {})(window.get());
                }
                auto result = ui->frame(window.publish_input_frame(),
                    {.fps = 60, .display_preview = preview, .vsync_active = vsync_active});
                if(!result)
                    return Result<void, GraphicsError>::failure({result.error().message});
                input_blocked = result.value().blocked;
                auto prepared = renderer.prepare_frame();
                if(!prepared)
                    return Result<void, GraphicsError>::failure(prepared.error());
                if(prepared.value() == Renderer::FramePreparation::Ready)
                    return renderer.render_frame();
            }
            return Result<void, GraphicsError>::failure(
                {"Project UI presentation remained deferred"});
        }
        void queue_key(int key) {
            pending_input = [key](GLFWwindow* window) {
                const auto callback = glfwSetKeyCallback(window, nullptr);
                glfwSetKeyCallback(window, callback);
                ASSERT_NE(callback, nullptr);
                callback(window, key, 0, GLFW_PRESS, 0);
                callback(window, key, 0, GLFW_RELEASE, 0);
            };
        }
        void queue_mouse(Math::Vec2 position, bool pressed) {
            pending_input = [position, pressed](GLFWwindow* window) {
                const auto cursor = glfwSetCursorPosCallback(window, nullptr);
                glfwSetCursorPosCallback(window, cursor);
                ASSERT_NE(cursor, nullptr);
                cursor(window, position.x, position.y);
                const auto mouse = glfwSetMouseButtonCallback(window, nullptr);
                glfwSetMouseButtonCallback(window, mouse);
                ASSERT_NE(mouse, nullptr);
                mouse(window, GLFW_MOUSE_BUTTON_LEFT, pressed ? GLFW_PRESS : GLFW_RELEASE, 0);
            };
        }
        Result<void> apply_display(const DisplaySettings& value) {
            if(!preview) {
                const auto requested = engine->get_renderer().request_output_settings(value.output);
                if(!requested)
                    return Result<void>::failure(requested.error().message);
                if(auto limit = engine->set_frame_rate_limit(value.frame_rate_limit); !limit)
                    return Result<void>::failure(limit.error().message);
            }
            active_display = value;
            ++display_applications;
            return Result<void>::success();
        }
        void type_display_size(const char* id, const std::string& value) {
            auto* input = document().GetElementById(id);
            ASSERT_NE(input, nullptr);
            ASSERT_TRUE(input->Focus(true));
            auto& context = *Rml::GetContext(0);
            context.ProcessKeyDown(Rml::Input::KI_A, Rml::Input::KM_CTRL);
            context.ProcessKeyUp(Rml::Input::KI_A, Rml::Input::KM_CTRL);
            context.ProcessKeyDown(Rml::Input::KI_BACK, 0);
            context.ProcessKeyUp(Rml::Input::KI_BACK, 0);
            if(!value.empty())
                context.ProcessTextInput(value);
            ASSERT_TRUE(submit_frame());
            EXPECT_EQ(input->GetAttribute("value", Rml::String{}), value);
        }
    };
    TEST_F(ProjectUiGpuTest, JourneyShowsChineseGuideAndChapterSelectionSendsItsNumber) {
        std::string entries;
        for(int i = 1; i <= 36; ++i) {
            if(!entries.empty())
                entries += '\n';
            entries += "光影旅程\t第 " + std::to_string(i) + " 站";
        }
        session_values = {{"tour.active", true}, {"tour.number", 1.f}, {"tour.total", 36.f},
            {"tour.title", std::string("材质长廊")}, {"tour.section", std::string("光影旅程")},
            {"tour.description", std::string("走近观察法线与金属高光")},
            {"tour.task", std::string("绕到玻璃另一侧")}, {"tour.entries", entries},
            {"tour.completed", std::string(36, '0')}, {"tour.has_reference", true}};
        ASSERT_TRUE(submit_frame());
        auto* guide = document().GetElementById("tour-guide");
        ASSERT_TRUE(guide && guide->IsVisible());
        EXPECT_NE(guide->GetInnerRML().find("材质长廊"), std::string::npos);
        click("tour-complete");
        ASSERT_EQ(game_events.back(), "tour.complete");
        click("tour-open");
        EXPECT_TRUE(ui->is_modal());
        EXPECT_TRUE(input_blocked);
        ASSERT_NE(document().GetElementById("tour-chapter-36"), nullptr);
        click("tour-chapter-36");
        ASSERT_EQ(game_events.back(), "tour.goto");
        ASSERT_TRUE(game_event_values.back());
        EXPECT_EQ(std::get<float>(*game_event_values.back()), 36.f);
        EXPECT_FALSE(ui->is_modal());
        click("tour-open");
        click("tour-close");
        EXPECT_FALSE(ui->is_modal());
        click("tour-open");
        queue_key(GLFW_KEY_ESCAPE);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(ui->is_modal());
        queue_key(GLFW_KEY_ESCAPE);
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_modal());
        queue_key(GLFW_KEY_ESCAPE);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(ui->is_modal());
        session_values.clear();
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(guide->IsVisible());
    }

    TEST_F(ProjectUiGpuTest, DemoHudReadsSessionAndClearsWhenTheGameStops) {
        session_values["character.score"] = 1.0f;
        session_values["character.finished"] = false;
        ASSERT_TRUE(submit_frame());
        auto* hint = document().GetElementById("game-hint");
        auto* status = document().GetElementById("game-status");
        ASSERT_NE(hint, nullptr);
        ASSERT_NE(status, nullptr);
        EXPECT_TRUE(hint->IsVisible());
        EXPECT_NE(status->GetInnerRML().find("1 / 3"), std::string::npos);
        session_values["character.score"] = 3.0f;
        session_values["character.finished"] = true;
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(status->GetInnerRML().find("挑战完成"), std::string::npos);
        session_values.clear();
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(hint->IsVisible());
    }

    TEST_F(ProjectUiGpuTest, CombatHudShowsHealthDefeatAndDeathWithoutCollectionActions) {
        session_values = {{"demo.mode", std::string("combat")}, {"combat.health.player", 120.f},
            {"combat.maximum", 120.f}, {"combat.defeated", 0.f}, {"combat.total", 2.f},
            {"character.score", 3.f}};
        ASSERT_TRUE(submit_frame());
        auto* hint = document().GetElementById("combat-hint");
        auto* health = document().GetElementById("combat-health");
        auto* status = document().GetElementById("combat-status");
        ASSERT_TRUE(hint && health && status);
        EXPECT_TRUE(hint->IsVisible());
        EXPECT_NE(health->GetInnerRML().find("120 / 120"), std::string::npos);
        EXPECT_FALSE(document().GetElementById("game-hint")->IsVisible());
        EXPECT_FALSE(document().GetElementById("course-actions")->IsVisible());
        session_values["combat.health.player"] = 72.f;
        session_values["combat.defeated"] = 2.f;
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(health->GetInnerRML().find("72 / 120"), std::string::npos);
        EXPECT_NE(status->GetInnerRML().find("挑战完成"), std::string::npos);
        session_values["combat.health.player"] = 0.f;
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(status->GetInnerRML().find("你已倒下"), std::string::npos);
        session_values.clear();
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(hint->IsVisible());
    }

    TEST_F(ProjectUiGpuTest, CourseButtonsSendEventsAndHideBehindSettingsOrWhenStopped) {
        session_values["character.level"] = 1.0f;
        session_values["character.score"] = 3.0f;
        session_values["character.finished"] = true;
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(document().GetElementById("course-next")->IsVisible());
        EXPECT_FALSE(document().GetElementById("course-return")->IsVisible());
        click("course-next");
        ASSERT_EQ(game_events.size(), 1u);
        EXPECT_EQ(game_events.back(), "character.next");
        session_values["character.level"] = 2.0f;
        session_values["character.score"] = 1.0f;
        session_values["character.finished"] = false;
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(document().GetElementById("course-next")->IsVisible());
        EXPECT_TRUE(document().GetElementById("course-return")->IsVisible());
        click("course-save");
        EXPECT_EQ(game_events.back(), "character.save");
        click("course-continue");
        EXPECT_EQ(game_events.back(), "character.continue");
        click("course-return");
        EXPECT_EQ(game_events.back(), "character.return");
        click("settings");
        EXPECT_FALSE(document().GetElementById("course-actions")->IsVisible());
        click("cancel");
        session_values.clear();
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(document().GetElementById("course-actions")->IsVisible());
        EXPECT_EQ(game_events.size(), 4u);
    }

    TEST_F(ProjectUiGpuTest, MenuFocusIsCapturedBeforeGpuRecording) {
        class FocusProbe final: public Rml::Element {
        public:
            using Rml::Element::Element;
            Rml::Element* observed = nullptr;
            unsigned calls = 0;

        protected:
            void OnRender() override {
                observed = GetContext()->GetFocusElement();
                ++calls;
            }
        };
        Rml::ElementInstancerGeneric<FocusProbe> instancer;
        auto owned = instancer.InstanceElement(&document(), "div", {});
        auto* probe = static_cast<FocusProbe*>(owned.get());
        probe->SetInstancer(&instancer);
        const ScopeExit release_probe([&] { document().RemoveChild(probe).reset(); });
        ASSERT_TRUE(probe->SetProperty("position", "absolute"));
        ASSERT_TRUE(probe->SetProperty("left", "0px"));
        ASSERT_TRUE(probe->SetProperty("top", "0px"));
        ASSERT_TRUE(probe->SetProperty("width", "4px"));
        ASSERT_TRUE(probe->SetProperty("height", "4px"));
        document().AppendChild(std::move(owned));
        document().GetElementById("settings")->Click();
        Input::Frame input;
        input.serial = 100;
        input.focused = true;
        auto updated = ui->frame(input, {.fps = 60});
        ASSERT_TRUE(updated) << updated.error();
        ASSERT_TRUE(ui->is_modal());
        auto* focused = Rml::GetContext(0)->GetFocusElement();
        ASSERT_NE(focused, nullptr);
        EXPECT_EQ(probe->observed, focused);
        EXPECT_EQ(probe->calls, 1u);
        auto& renderer = engine->get_renderer();
        auto prepared = renderer.prepare_frame();
        ASSERT_TRUE(prepared) << prepared.error();
        ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        ASSERT_TRUE(renderer.render_frame());
        EXPECT_EQ(probe->calls, 1u);
    }

    TEST_F(ProjectUiGpuTest, DisplayTrialSurvivesReloadAndConfirmsOnlyAfterOutputIsApplied) {
        display_services = true;
        display_confirmation_services = true;
        create_ui(InputActions{});
        click("settings");
        click("display-size");
        click("display-output-mode");
        click("display-apply");
        ASSERT_TRUE(display_confirmation->is_pending());
        EXPECT_FALSE(std::filesystem::exists(documents.path() / "display.json"));
        EXPECT_TRUE(document().GetElementById("display-apply")->HasAttribute("disabled"));
        EXPECT_TRUE(document().GetElementById("cancel")->HasAttribute("disabled"));
        display_time += std::chrono::seconds(10);
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(display_confirmation->remaining(display_time), 5);
        EXPECT_NE(document().GetElementById("display-confirmation")->GetInnerRML().find("5 秒"),
            std::string::npos);
        EXPECT_FALSE(document().GetElementById("display-confirm")->HasAttribute("disabled"));
        EXPECT_EQ(
            Rml::GetContext(0)->GetFocusElement(), document().GetElementById("display-confirm"));
        queue_key(GLFW_KEY_ENTER);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(display_confirmation->is_pending());
        EXPECT_EQ(display_applications, 1u);
        const auto saved = PlayerDisplaySettings::load(
            display_project_id, saved_display, documents.path() / "display.json");
        ASSERT_TRUE(saved) << saved.error();
        EXPECT_EQ(saved.value().settings(), active_display);
        EXPECT_EQ(active_display.width, 1280);
        EXPECT_EQ(active_display.output.mode, OutputMode::Hdr);
        EXPECT_FALSE(document().GetElementById("cancel")->HasAttribute("disabled"));
    }

    TEST_F(ProjectUiGpuTest, DisplayTrialExpiresOrEscRevertsAndSaveFailureKeepsConfirmation) {
        display_services = true;
        display_confirmation_services = true;
        create_ui(InputActions{});
        const auto original = active_display;
        click("settings");
        type_display_size("display-width", "1377");
        click("display-apply");
        ASSERT_TRUE(display_confirmation->is_pending());
        display_time += std::chrono::seconds(15);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(display_confirmation->is_pending());
        EXPECT_EQ(active_display, original);
        EXPECT_EQ(document().GetElementById("display-width")->GetAttribute("value", Rml::String{}),
            "960");
        EXPECT_TRUE(ui->is_modal());

        click("display-size");
        click("display-apply");
        ASSERT_TRUE(display_confirmation->is_pending());
        std::filesystem::create_directory(documents.path() / "display.json");
        click("display-confirm");
        EXPECT_TRUE(display_confirmation->is_pending());
        EXPECT_FALSE(document().GetElementById("display-error")->GetInnerRML().empty());
        EXPECT_FALSE(document().GetElementById("display-revert")->HasAttribute("disabled"));
        queue_key(GLFW_KEY_ESCAPE);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(display_confirmation->is_pending());
        EXPECT_EQ(active_display, original);
        EXPECT_TRUE(ui->is_modal());
        EXPECT_EQ(display_player->settings(), original);
    }

    TEST_F(ProjectUiGpuTest, AudioDraftCancelsAndFailedSavePreservesSliderValuesAcrossReload) {
        audio_services = true;
        create_ui(InputActions{});
        const auto change = [&](const char* id, int value) {
            auto* slider = document().GetElementById(id);
            ASSERT_NE(slider, nullptr);
            Rml::Dictionary parameters;
            parameters["value"] = value;
            slider->DispatchEvent("change", parameters);
            ASSERT_TRUE(submit_frame());
        };
        click("settings");
        change("audio-master", 25);
        change("audio-effects", 0);
        click("cancel");
        EXPECT_EQ(audio_applications, 0u);
        EXPECT_EQ(engine->get_audio_settings(), AudioSettings{});
        click("settings");
        change("audio-master", 25);
        change("audio-effects", 0);
        change("audio-music", 50);
        fail_audio_save = true;
        click("audio-apply");
        EXPECT_EQ(audio_applications, 0u);
        EXPECT_EQ(engine->get_audio_settings(), AudioSettings{});
        EXPECT_NE(document().GetElementById("audio-error")->GetInnerRML().find("save failure"),
            std::string::npos);
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(document().GetElementById("audio-master")->GetAttribute("value", 0.0f), 25);
        fail_audio_save = false;
        click("audio-apply");
        EXPECT_EQ(audio_applications, 1u);
        EXPECT_EQ(saved_audio, (AudioSettings{0.25f, 0, 0.5f}));
        EXPECT_EQ(engine->get_audio_settings(), saved_audio);
        EXPECT_NE(document().GetElementById("audio-active")->GetInnerRML().find("25%"),
            std::string::npos);
        click("audio-restore");
        EXPECT_EQ(audio_applications, 1u);
        click("audio-apply");
        EXPECT_EQ(saved_audio, (AudioSettings{0.7f, 0.6f, 0.5f}));
        EXPECT_TRUE(ui->is_modal());
    }

    TEST_F(ProjectUiGpuTest, QualityDraftSurvivesSaveFailureAndReloadAndReportsActiveRenderer) {
        quality_services = true;
        create_ui(InputActions{});
        const auto window_size = engine->get_window().get_size();
        const auto initial = engine->get_renderer().get_quality_settings();
        click("settings");
        click("quality-scale");
        click("cancel");
        EXPECT_EQ(quality_applications, 0u);
        EXPECT_EQ(saved_quality.render_scale, 1);
        click("settings");
        click("quality-scale");
        click("quality-msaa");
        for(std::size_t i = 0; i < engine->get_renderer().supported_msaa_samples().size(); ++i) {
            if(document().GetElementById("quality-msaa")->GetInnerRML().find("TAA")
                != std::string::npos)
                break;
            click("quality-msaa");
        }
        ASSERT_NE(document().GetElementById("quality-msaa")->GetInnerRML().find("TAA"),
            std::string::npos);
        fail_quality_save = true;
        click("quality-apply");
        EXPECT_EQ(quality_applications, 0u);
        EXPECT_EQ(engine->get_renderer().get_quality_settings(), initial);
        EXPECT_NE(document().GetElementById("quality-error")->GetInnerRML().find("save failure"),
            std::string::npos);
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(document().GetElementById("quality-scale")->GetInnerRML().find("50%"),
            std::string::npos);
        fail_quality_save = false;
        click("quality-apply");
        EXPECT_EQ(quality_applications, 1u);
        EXPECT_EQ(saved_quality.render_scale, 0.5f);
        EXPECT_TRUE(saved_quality.temporal_aa);
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(engine->get_renderer().get_quality_settings().render_scale, 0.5f);
        EXPECT_TRUE(engine->get_renderer().get_quality_settings().temporal_aa);
        EXPECT_NE(document().GetElementById("quality-active")->GetInnerRML().find("50%"),
            std::string::npos);
        EXPECT_EQ(engine->get_window().get_size(), window_size);
        EXPECT_TRUE(ui->is_modal());
        const auto& samples = engine->get_renderer().supported_msaa_samples();
        EXPECT_NE(std::ranges::find(samples, saved_quality.msaa_samples), samples.end());
    }

    TEST_F(ProjectUiGpuTest, DeferredPathSwitchesAtRuntimeAndKeepsSingleSampleTaa) {
        quality_services = true;
        create_ui(InputActions{});
        click("settings");
        click("quality-path");
        click("quality-balanced");
        EXPECT_NE(document().GetElementById("quality-path")->GetInnerRML().find("Deferred"),
            std::string::npos);
        EXPECT_EQ(quality_applications, 0u);
        click("quality-msaa");
        click("quality-apply");
        ASSERT_EQ(quality_applications, 1u);
        EXPECT_EQ(saved_quality.render_path, RenderPath::Deferred);
        EXPECT_EQ(saved_quality.msaa_samples, 1u);
        EXPECT_TRUE(saved_quality.temporal_aa);
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(engine->get_renderer().get_quality_settings(), saved_quality);
        click("quality-restore");
        click("quality-apply");
        ASSERT_EQ(quality_applications, 2u);
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(engine->get_renderer().get_quality_settings(), default_quality);
    }

    TEST_F(ProjectUiGpuTest, QualityPresetsStageValuesAndKeepCustomDefaultsAndSavedParameters) {
        quality_services = true;
        saved_quality = {1, 1, 0.6f};
        default_quality = {1, 1, 0.9f};
        create_ui(InputActions{});
        click("settings");
        EXPECT_NE(document().GetElementById("quality-preset")->GetInnerRML().find("自定义"),
            std::string::npos);
        click("quality-performance");
        EXPECT_NE(document().GetElementById("quality-preset")->GetInnerRML().find("性能"),
            std::string::npos);
        EXPECT_EQ(quality_applications, 0u);
        click("cancel");
        click("settings");
        EXPECT_NE(document().GetElementById("quality-scale")->GetInnerRML().find("60%"),
            std::string::npos);
        click("quality-balanced");
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(document().GetElementById("quality-preset")->GetInnerRML().find("均衡"),
            std::string::npos);
        click("quality-quality");
        click("quality-scale");
        EXPECT_NE(document().GetElementById("quality-preset")->GetInnerRML().find("自定义"),
            std::string::npos);
        click("quality-restore");
        EXPECT_NE(document().GetElementById("quality-scale")->GetInnerRML().find("90%"),
            std::string::npos);
        EXPECT_NE(document().GetElementById("quality-preset")->GetInnerRML().find("自定义"),
            std::string::npos);
        EXPECT_EQ(saved_quality, (QualitySettings{1, 1, 0.6f}));
        click("quality-performance");
        click("quality-apply");
        ASSERT_EQ(quality_applications, 1u);
        EXPECT_EQ(saved_quality,
            (QualitySettings{1, std::min(2.0f, engine->get_renderer().max_anisotropy()), 0.75f}));
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(engine->get_renderer().get_quality_settings(), saved_quality);
        click("cancel");
        click("settings");
        EXPECT_NE(document().GetElementById("quality-preset")->GetInnerRML().find("性能"),
            std::string::npos);
    }

    TEST_F(ProjectUiGpuTest, QualityPresetsRespectSparseSampleCountsAndCollapsedDeviceLimits) {
        // 用受限设备报告驱动同一个 demo 控制器，实际渲染器仍校验最终的 1× 参数。
        write("runtime.ui.lua", "local controller = (function()\n" + original_controller + R"lua(
end)()
local event = controller.on_event
controller.on_event = function(self, ui, ...)
    local limited = {}
    for name, callback in pairs(ui) do limited[name] = callback end
    limited.quality_settings = function()
        local settings = ui.quality_settings()
        settings.supported_msaa = {1}
        settings.max_anisotropy_supported = 1
        return settings
    end
    return event(self, limited, ...)
end
return controller
)lua");
        quality_services = true;
        create_ui(InputActions{});
        click("settings");
        for(const auto* preset : {"quality-balanced", "quality-quality"}) {
            click(preset);
            const auto label = document().GetElementById("quality-preset")->GetInnerRML();
            EXPECT_NE(label.find("均衡 / 质量"), std::string::npos);
            click("quality-apply");
            EXPECT_EQ(saved_quality, (QualitySettings{1, 1, 1}));
        }
        click("quality-performance");
        click("quality-apply");
        EXPECT_EQ(saved_quality, (QualitySettings{1, 1, 0.75f}));
    }

    TEST_F(ProjectUiGpuTest, DisplayDraftCancelsAndSaveFailurePreservesItAcrossReload) {
        display_services = true;
        create_ui(InputActions{});
        click("settings");
        click("display-size");
        click("display-vsync");
        click("display-output-mode");
        click("cancel");
        EXPECT_EQ(display_applications, 0U);
        EXPECT_EQ(saved_display, DisplaySettings{});
        click("settings");
        click("display-size");
        click("display-vsync");
        click("display-output-mode");
        fail_display_save = true;
        click("display-apply");
        EXPECT_EQ(display_applications, 0U);
        EXPECT_FALSE(document().GetElementById("display-error")->GetInnerRML().empty());
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(document().GetElementById("display-size")->GetInnerRML().find("1280"),
            std::string::npos);
        fail_display_save = false;
        click("display-apply");
        EXPECT_EQ(display_applications, 1U);
        EXPECT_EQ(saved_display,
            (DisplaySettings{1280, 720, WindowMode::Windowed, true, {OutputMode::Hdr, 4, 1}}));
        EXPECT_EQ(engine->get_renderer().get_output_settings(), saved_display.output);
        ASSERT_TRUE(submit_frame());
        const auto actual_output =
            document().GetElementById("display-output-active")->GetInnerRML();
        EXPECT_NE(actual_output.find(engine->get_renderer().is_hdr_output() ? "HDR" : "SDR"),
            std::string::npos);
        EXPECT_TRUE(ui->is_modal());
    }

    TEST_F(ProjectUiGpuTest, FrameRateDraftValidatesCancelsAndSurvivesSaveFailureAndReload) {
        display_services = true;
        saved_display.frame_rate_limit = 75;
        create_ui(InputActions{});
        click("settings");
        EXPECT_EQ(
            document().GetElementById("display-limit")->GetAttribute("value", Rml::String{}), "75");
        for(const auto* value : {"0", "30", "60", "120", "144", "240", "0"}) {
            click("display-limit-preset");
            EXPECT_EQ(
                document().GetElementById("display-limit")->GetAttribute("value", Rml::String{}),
                value);
        }
        click("cancel");
        EXPECT_EQ(display_applications, 0u);
        click("settings");
        for(const auto* value : {"", "-1", "12.5", "1e2", "1001"}) {
            type_display_size("display-limit", value);
            click("display-apply");
            EXPECT_EQ(display_applications, 0u);
            EXPECT_FALSE(document().GetElementById("display-error")->GetInnerRML().empty());
        }
        type_display_size("display-limit", "120");
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(document().GetElementById("display-limit")->GetAttribute("value", Rml::String{}),
            "120");
        fail_display_save = true;
        click("display-apply");
        EXPECT_EQ(saved_display.frame_rate_limit, 75);
        EXPECT_EQ(engine->frame_rate_limit(), 0);
        fail_display_save = false;
        click("display-apply");
        EXPECT_EQ(saved_display.frame_rate_limit, 120);
        EXPECT_EQ(engine->frame_rate_limit(), 120);
        click("display-restore");
        click("display-apply");
        EXPECT_EQ(saved_display.frame_rate_limit, 0);
        EXPECT_EQ(engine->frame_rate_limit(), 0);
    }

    TEST_F(ProjectUiGpuTest, PreviewDisplayControlsKeepStandaloneModeVsyncAndHdr) {
        display_services = true;
        preview = true;
        saved_display.mode = WindowMode::Fullscreen;
        saved_display.output = {OutputMode::Hdr, 8, 1.25f};
        saved_display.frame_rate_limit = 144;
        const auto standalone_output = saved_display.output;
        create_ui(InputActions{});
        const auto editor_size = engine->get_window().get_size();
        click("settings");
        EXPECT_TRUE(document().GetElementById("display-mode")->HasAttribute("disabled"));
        EXPECT_TRUE(document().GetElementById("display-vsync")->HasAttribute("disabled"));
        EXPECT_TRUE(document().GetElementById("display-limit")->HasAttribute("disabled"));
        EXPECT_TRUE(document().GetElementById("display-limit-preset")->HasAttribute("disabled"));
        for(const auto* id : {"display-output-mode", "display-hdr-headroom", "display-hdr-white"})
            EXPECT_TRUE(document().GetElementById(id)->HasAttribute("disabled")) << id;
        click("display-restore");
        click("display-apply");
        EXPECT_EQ(saved_display.width, 1280);
        EXPECT_EQ(saved_display.mode, WindowMode::Fullscreen);
        EXPECT_FALSE(saved_display.vsync);
        EXPECT_EQ(saved_display.output, standalone_output);
        EXPECT_EQ(saved_display.frame_rate_limit, 144);
        EXPECT_EQ(engine->frame_rate_limit(), 0);
        EXPECT_EQ(engine->get_window().get_size(), editor_size);
    }

    TEST_F(ProjectUiGpuTest, CustomDisplaySizeValidatesWithoutLosingTheMenuOrReloadDraft) {
        display_services = true;
        create_ui(InputActions{});
        click("settings");
        type_display_size("display-width", "1377");
        type_display_size("display-height", "811");
        click("cancel");
        EXPECT_EQ(display_applications, 0U);
        click("settings");
        EXPECT_EQ(document().GetElementById("display-width")->GetAttribute("value", Rml::String{}),
            "960");
        type_display_size("display-height", "811");
        for(const auto* value : {"", "0", "-1", "12.5", "1e3", "2147483648"}) {
            SCOPED_TRACE(value);
            type_display_size("display-width", value);
            click("display-apply");
            EXPECT_EQ(display_applications, 0U);
            EXPECT_TRUE(ui->is_modal());
            EXPECT_FALSE(document().GetElementById("display-error")->GetInnerRML().empty());
        }
        type_display_size("display-width", "1377");
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(document().GetElementById("display-width")->GetAttribute("value", Rml::String{}),
            "1377");
        EXPECT_EQ(document().GetElementById("display-height")->GetAttribute("value", Rml::String{}),
            "811");
        click("display-apply");
        EXPECT_EQ(display_applications, 1U);
        EXPECT_EQ(saved_display, (DisplaySettings{1377, 811, WindowMode::Windowed, false}));
    }

    TEST_F(ProjectUiGpuTest, ActiveVsyncFeedbackDoesNotFollowAnUnappliedDraft) {
        display_services = true;
        vsync_active = false;
        create_ui(InputActions{});
        click("settings");
        click("display-vsync");
        EXPECT_FALSE(saved_display.vsync);
        EXPECT_NE(document().GetElementById("display-vsync")->GetInnerRML().find("开启"),
            std::string::npos);
        EXPECT_NE(document().GetElementById("display-active-vsync")->GetInnerRML().find("关闭"),
            std::string::npos);
        click("display-apply");
        EXPECT_TRUE(saved_display.vsync);
        EXPECT_NE(document().GetElementById("display-active-vsync")->GetInnerRML().find("关闭"),
            std::string::npos);
        vsync_active = true;
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(document().GetElementById("display-active-vsync")->GetInnerRML().find("开启"),
            std::string::npos);
    }
    TEST_F(ProjectUiGpuTest, DemoMenuLayoutFitsAfterOpeningFromSettings) {
        auto project = Project::load(std::filesystem::path(PROJECT_ROOT_DIR) / "demo");
        ASSERT_TRUE(project) << project.error();
        auto* context = Rml::GetContext(0);
        ASSERT_NE(context, nullptr);
        auto* document = context->GetDocument(0);
        ASSERT_NE(document, nullptr);
        document->GetElementById("settings")->Click();
        ASSERT_TRUE(submit_frame());
        struct Viewport {
            Rml::Vector2i size;
            float density;
        };
        const std::array viewports{Viewport{{960, 720}, 1}, Viewport{{1920, 1440}, 2},
            Viewport{{640, 480}, 1}, Viewport{{1280, 960}, 2}, Viewport{{480, 360}, 1}};
        const auto check_layout = [&](const std::size_t binding_count) {
            for(const auto& viewport : viewports) {
                SCOPED_TRACE(::testing::Message() << viewport.size.x << "x" << viewport.size.y
                                                  << " @ " << viewport.density);
                context->SetDimensions(viewport.size);
                context->SetDensityIndependentPixelRatio(viewport.density);
                ASSERT_TRUE(context->Update());
                auto* panel = document->GetElementById("panel");
                panel->SetScrollTop(0);
                const auto offset = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
                EXPECT_GE(offset.x, 0);
                EXPECT_GE(offset.y, 0);
                EXPECT_LE(offset.x + size.x, viewport.size.x);
                EXPECT_LE(offset.y + size.y, viewport.size.y);
                const auto content_width = panel->GetClientWidth();
                EXPECT_GT(document->GetElementById("action-selector")->GetClientWidth(),
                    content_width * 0.75f);
                auto* bindings = document->GetElementById("bindings");
                EXPECT_GT(bindings->GetClientWidth(), content_width * 0.75f);
                ASSERT_EQ(bindings->GetNumChildren(), binding_count);
                float previous_bottom = 0;
                for(int index = 0; index < bindings->GetNumChildren(); ++index) {
                    auto* row = bindings->GetChild(index);
                    const auto row_offset = row->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const auto row_size = row->GetBox().GetSize(Rml::BoxArea::Border);
                    EXPECT_GT(row_size.x, content_width * 0.75f);
                    EXPECT_GE(row_offset.y, previous_bottom);
                    previous_bottom = row_offset.y + row_size.y;
                    for(int child_index = 0; child_index < row->GetNumChildren(); ++child_index) {
                        auto* child = row->GetChild(child_index);
                        const auto child_offset = child->GetAbsoluteOffset(Rml::BoxArea::Border);
                        const auto child_size = child->GetBox().GetSize(Rml::BoxArea::Border);
                        EXPECT_GE(child_offset.x, row_offset.x);
                        EXPECT_LE(child_offset.x + child_size.x, row_offset.x + row_size.x + 1);
                        EXPECT_GE(child_offset.y, row_offset.y);
                        EXPECT_LE(child_offset.y + child_size.y, previous_bottom + 1);
                    }
                }
                EXPECT_GE(
                    document->GetElementById("status")->GetAbsoluteOffset().y, previous_bottom);
                EXPECT_GT(panel->GetScrollHeight(), panel->GetClientHeight());
                ASSERT_TRUE(document->GetElementById("cancel")->Focus(true));
                context->ProcessKeyDown(Rml::Input::KI_TAB, 0);
                context->ProcessKeyUp(Rml::Input::KI_TAB, 0);
                EXPECT_EQ(context->GetFocusElement(), document->GetElementById("apply"));
                EXPECT_GT(panel->GetScrollTop(), 0);
            }
        };
        const auto& actions = project.value().input_actions().actions();
        ASSERT_GT(actions.size(), 1u);
        ASSERT_GT(actions[0].bindings.size(), 1u);
        ASSERT_GT(actions[1].bindings.size(), actions[0].bindings.size());
        check_layout(actions[0].bindings.size());
        document->GetElementById("toggle_binding-" + actions[0].bindings[0].id.to_string())
            ->Click();
        EXPECT_TRUE(ui->frame(engine->get_window().publish_input_frame(), {.fps = 60}));
        check_layout(actions[0].bindings.size());
        document->GetElementById("next")->Click();
        EXPECT_TRUE(ui->frame(engine->get_window().publish_input_frame(), {.fps = 60}));
        check_layout(actions[1].bindings.size());
        ASSERT_TRUE(submit_frame());
    }

    TEST_F(ProjectUiGpuTest, MousePressAcrossFramesActivatesMenuControls) {
        auto& window = engine->get_window();
        glfwSetWindowSize(window.get(), 960, 720);
        glfwFocusWindow(window.get());
        window.poll_events();
        const auto focus = glfwSetWindowFocusCallback(window.get(), nullptr);
        glfwSetWindowFocusCallback(window.get(), focus);
        ASSERT_NE(focus, nullptr);
        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        window.discard_pending_input();
        click("settings");
        ASSERT_TRUE(submit_frame());
        auto* previous = document().GetElementById("previous");
        ASSERT_NE(previous, nullptr);
        const auto offset = previous->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = previous->GetBox().GetSize(Rml::BoxArea::Border);
        const auto dimensions = Rml::GetContext(0)->GetDimensions();
        const auto window_size = window.get_size();
        const Math::Vec2 position{(offset.x + size.x / 2) * window_size.x / dimensions.x,
            (offset.y + size.y / 2) * window_size.y / dimensions.y};
        queue_mouse(position, true);
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(Rml::GetContext(0)->GetFocusElement(), previous);
        queue_mouse(position, false);
        ASSERT_TRUE(submit_frame());
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        EXPECT_NE(document()
                      .GetElementById("action-name")
                      ->GetInnerRML()
                      .find(project.value().input_actions().actions().back().name),
            std::string::npos);
    }

    TEST_F(ProjectUiGpuTest, ResourceReferencesPublishOnlyWithAValidCandidate) {
        click("settings");
        auto* live = &document();
        const auto before = ui->resource_dependencies();
        auto candidate = original_document;
        const auto body_end = candidate.find("</body>");
        ASSERT_NE(body_end, std::string::npos);
        candidate.insert(body_end, R"(<div style="display: none"><img src="missing.png" /></div>)");
        write("runtime.rml", candidate);
        const auto missing = ui->reload();
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().find("missing.png"), std::string::npos);
        EXPECT_EQ(&document(), live);
        EXPECT_EQ(ui->resource_dependencies(), before);
        EXPECT_TRUE(ui->is_modal());
        write("extra.rcss", "p { color: white; }");
        candidate = original_document;
        candidate.insert(
            candidate.find("</head>"), R"(<link type="text/rcss" href="extra.rcss" />)");
        write("runtime.rml", candidate);
        ASSERT_TRUE(ui->reload());
        EXPECT_NE(&document(), live);
        EXPECT_TRUE(ui->is_modal());
        EXPECT_TRUE(
            std::ranges::find(ui->resource_dependencies(), std::filesystem::path("ui/extra.rcss"))
            != ui->resource_dependencies().end());
        write("runtime.rml", original_document);
        ASSERT_TRUE(ui->reload());
        EXPECT_EQ(ui->resource_dependencies(), before);
    }

    TEST_F(ProjectUiGpuTest, CandidateFailuresPreservePageControllerAndInputDraft) {
        click("settings");
        ASSERT_TRUE(ui->is_modal());
        const auto project = Project::load(std::filesystem::path(PROJECT_ROOT_DIR) / "demo");
        ASSERT_TRUE(project);
        const auto& action = project.value().input_actions().actions().front();
        click("toggle_binding-" + action.bindings.front().id.to_string());
        auto* live = &document();
        auto* context = Rml::GetContext(0);
        auto* focused = context->GetFocusElement();
        for(const auto& script :
            {std::string("return {on_mount=42,model={}}"), std::string("while true do end"),
                original_controller + "\nerror('candidate failed')"}) {
            write("runtime.ui.lua", script);
            EXPECT_FALSE(ui->reload());
            EXPECT_EQ(&document(), live);
            ASSERT_TRUE(submit_frame());
            EXPECT_TRUE(ui->is_modal());
        }
        for(unsigned attempt = 0; attempt < 3; ++attempt) {
            std::string script = "return {model={";
            for(unsigned field = 0; field < 65; ++field)
                script +=
                    "candidate_" + std::to_string(attempt) + "_" + std::to_string(field) + "=0,";
            script += "},on_mount=function(self,ui) ui.require_element('missing-candidate') end}";
            write("runtime.ui.lua", script);
            const auto rejected = ui->reload();
            ASSERT_FALSE(rejected);
            EXPECT_NE(rejected.error().find("missing-candidate"), std::string::npos);
            EXPECT_EQ(Rml::GetContext(0), context);
            EXPECT_EQ(context->GetFocusElement(), focused);
            EXPECT_EQ(&document(), live);
            EXPECT_EQ(Rml::GetNumContexts(), 1);
            ASSERT_TRUE(submit_frame());
        }
        click("next");
        EXPECT_EQ(document().GetElementById("action-name")->GetInnerRML(),
            project.value().input_actions().actions()[1].name);
        click("previous");
        EXPECT_EQ(document().GetElementById("action-name")->GetInnerRML(), action.name);
        write("runtime.ui.lua", original_controller);
        auto missing = original_document;
        const auto id = missing.find("id=\"apply\"");
        ASSERT_NE(id, std::string::npos);
        missing.replace(id, std::strlen("id=\"apply\""), "id=\"missing-apply\"");
        missing.replace(missing.find("<body data-model=\"ui\">"),
            std::strlen("<body data-model=\"ui\">"),
            "<body data-model=\"ui\" data-event-load=\"command('restore')\">");
        write("runtime.rml", missing);
        const auto rejected = ui->reload();
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().find("apply"), std::string::npos);
        EXPECT_EQ(&document(), live);
        write("runtime.rml", original_document);
        auto extended = original_controller;
        const auto extra = extended.find("model = {");
        ASSERT_NE(extra, std::string::npos);
        extended.insert(extra + std::strlen("model = {"), "reload_probe = 'fresh', ");
        write("runtime.ui.lua", extended);
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(Rml::GetNumContexts(), 1);
        EXPECT_TRUE(ui->is_modal());
        click("apply");
        ASSERT_EQ(submitted.size(), 1u);
        ASSERT_EQ(submitted.front().actions().size(), 1u);
        const auto& patch = submitted.front().actions().front();
        EXPECT_EQ(patch.id, action.id);
        EXPECT_TRUE(patch.bindings.front().disabled);
        EXPECT_FALSE(ui->is_modal());
        EXPECT_TRUE(input_blocked);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(input_blocked);
    }

    TEST_F(ProjectUiGpuTest, SaveFailureAllowsRetryAndDeactivationCancelsDraft) {
        click("settings");
        fail_save = true;
        click("apply");
        EXPECT_TRUE(ui->is_modal());
        EXPECT_NE(document().GetElementById("error")->GetInnerRML().find("Simulated save failure"),
            std::string::npos);
        ASSERT_EQ(submitted.size(), 1u);
        fail_save = false;
        click("apply");
        ASSERT_EQ(submitted.size(), 2u);
        EXPECT_EQ(submitted[0], submitted[1]);
        EXPECT_FALSE(ui->is_modal());
        click("settings");
        ui->deactivate();
        EXPECT_FALSE(ui->is_modal());
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(loads, 2u);
        click("settings");
        EXPECT_EQ(loads, 3u);
    }

    TEST_F(ProjectUiGpuTest, IndependentProjectUsesItsOwnPageModelAndCommands) {
        write("other.rml", R"rml(<rml><head><style>body {font-family: Comet; font-weight: bold;}
            button {width: 200px; height: 50px;}</style></head><body data-model="ui">
            <button id="toggle" data-event-click="command('toggle')">{{ label }}</button></body></rml>)rml");
        write("other.ui.lua", R"lua(
            return {model={label="Open"},state={open=false},
                on_event=function(self,ui,command)
                    assert(command=="toggle")
                    self.state.open=not self.state.open
                    ui.set("label",self.state.open and "Close" or "Open")
                    ui.modal(self.state.open)
                end}
        )lua");
        create_ui({}, {"ui/other.rml", "ui/other.ui.lua"});
        EXPECT_EQ(document().GetElementById("settings"), nullptr);
        click("toggle");
        EXPECT_TRUE(ui->is_modal());
        EXPECT_EQ(loads, 0u);
        EXPECT_NE(
            document().GetElementById("toggle")->GetInnerRML().find("Close"), std::string::npos);
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        click("toggle");
        EXPECT_FALSE(ui->is_modal());
    }
    TEST_F(ProjectUiGpuTest, BuiltinInputMenuUsesProjectElementsShortcutsAndBindingMarkup) {
        write("other.rml", R"rml(<rml><head><style>
            body {font-family: Comet; font-weight: bold;}
            button {width: 200px; height: 45px;} #dialog {display: none;}
            </style></head><body data-model="ui">
            <button id="launch" data-event-click="command('open')">Open</button>
            <div id="dialog"><div id="card"><div>{{action_name}}</div><div id="slots"></div>
            <button id="reset" data-event-click="command('restore')">Reset</button>
            <button id="save" data-event-click="command('apply')">Save</button>
            <button id="dismiss" data-event-click="command('cancel')">Cancel</button>
            </div></div></body></rml>)rml");
        write("other.ui.lua", R"lua(return ui.settings {
            elements={open="launch",menu="dialog",panel="card",bindings="slots",
                restore="reset",apply="save",cancel="dismiss"},
            shortcuts={open="F2",reload=false},
            render_bindings=function(action)
                return '<button id="custom-binding" data-event-click="command(\'toggle_binding\',\''
                    ..action.id..'\',\''..action.bindings[1].id..'\')">Toggle</button>'
            end,
        })lua");
        const auto action_id = Uuid::generate();
        const auto binding_id = Uuid::generate();
        const auto actions = InputActions::create({{.name = "custom.action",
            .bindings = {{.control = Input::Key::A, .id = binding_id}},
            .id = action_id}});
        ASSERT_TRUE(actions) << actions.error();
        create_ui(actions.value(), {"ui/other.rml", "ui/other.ui.lua"});
        EXPECT_EQ(document().GetElementById("settings"), nullptr);
        queue_key(GLFW_KEY_F1);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(ui->is_modal());
        queue_key(GLFW_KEY_F2);
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_modal());
        EXPECT_EQ(loads, 1u);
        click("custom-binding");
        click("save");
        ASSERT_EQ(submitted.size(), 1u);
        ASSERT_EQ(submitted.front().actions().size(), 1u);
        const auto& patch = submitted.front().actions().front();
        EXPECT_EQ(patch.id, action_id);
        ASSERT_EQ(patch.bindings.size(), 1u);
        EXPECT_EQ(patch.bindings.front().id, binding_id);
        EXPECT_TRUE(patch.bindings.front().disabled);
        EXPECT_FALSE(ui->is_modal());
    }

    TEST_F(ProjectUiGpuTest, BuiltinAudioMenuReloadKeepsDraftAndInitializesNewProjectState) {
        write("other.rml", R"rml(<rml><head><style>
            body {font-family: Comet; font-weight: bold;}
            button {width: 200px; height: 45px;} #dialog {display: none;}
            </style></head><body data-model="ui">
            <div id="probe">{{probe}}</div>
            <button id="launch" data-event-click="command('open')">Open</button>
            <div id="dialog"><div id="card"><div id="volume">{{audio_master}}</div>
            <button id="reduce" data-event-click="command('audio_master',42)">Reduce</button>
            <button id="save" data-event-click="command('audio_apply')">Save</button>
            <button id="dismiss" data-event-click="command('cancel')">Cancel</button>
            </div></div></body></rml>)rml");
        const auto controller = [](const std::string& project_state) {
            return R"lua(return ui.settings {input=false,audio=true,
                elements={open="launch",menu="dialog",panel="card",cancel="dismiss"},
                model={probe=""},state={)lua"
                   + project_state + R"lua(},
                on_mount=function(self,api)
                    api.set("probe",string.format("%g/%s/%s",self.state.kept,
                        tostring(self.state.added),tostring(self.state.changed)))
                end,
            })lua";
        };
        write("other.ui.lua", controller("kept=3,changed='old'"));
        audio_services = true;
        create_ui({}, {"ui/other.rml", "ui/other.ui.lua"});
        click("launch");
        click("reduce");
        EXPECT_EQ(document().GetElementById("volume")->GetInnerRML(), "42");
        write("other.ui.lua", controller("kept=0,added=7,changed=9"));
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_modal());
        EXPECT_EQ(document().GetElementById("volume")->GetInnerRML(), "42");
        EXPECT_EQ(document().GetElementById("probe")->GetInnerRML(), "3/7/9");
        EXPECT_EQ(loads, 0u);
        EXPECT_EQ(audio_applications, 0u);
        click("save");
        EXPECT_EQ(audio_applications, 1u);
        EXPECT_FLOAT_EQ(saved_audio.master_volume, 0.42f);
        EXPECT_TRUE(ui->is_modal());
        click("dismiss");
        EXPECT_FALSE(ui->is_modal());
        EXPECT_TRUE(submitted.empty());
    }

    TEST_F(ProjectUiGpuTest, CaptureStopsTheCurrentInputBatchAndUsesTheFollowingFrame) {
        auto& window = engine->get_window();
        glfwFocusWindow(window.get());
        window.poll_events();
        const auto focus = glfwSetWindowFocusCallback(window.get(), nullptr);
        glfwSetWindowFocusCallback(window.get(), focus);
        ASSERT_NE(focus, nullptr);
        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        window.discard_pending_input();
        click("settings");
        const auto project = Project::load(std::filesystem::path(PROJECT_ROOT_DIR) / "demo");
        ASSERT_TRUE(project);
        const auto& action = project.value().input_actions().actions().front();
        auto* key = document().GetElementById("key-" + action.bindings.front().id.to_string());
        ASSERT_NE(key, nullptr);
        EXPECT_EQ(Rml::GetContext(0)->GetFocusElement(), key);
        pending_input = [](GLFWwindow* native_window) {
            const auto callback = glfwSetKeyCallback(native_window, nullptr);
            glfwSetKeyCallback(native_window, callback);
            ASSERT_NE(callback, nullptr);
            for(const int key : {GLFW_KEY_ENTER, GLFW_KEY_TAB, GLFW_KEY_ENTER}) {
                callback(native_window, key, 0, GLFW_PRESS, 0);
                callback(native_window, key, 0, GLFW_RELEASE, 0);
            }
        };
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(submitted.empty());
        EXPECT_TRUE(ui->is_modal());
        EXPECT_TRUE(input_blocked);
        EXPECT_NE(
            document().GetElementById("status")->GetInnerRML().find("等待输入"), std::string::npos);
        queue_key(GLFW_KEY_K);
        ASSERT_TRUE(submit_frame());
        click("apply");
        ASSERT_EQ(submitted.size(), 1u);
        ASSERT_EQ(submitted[0].actions().size(), 1u);
        const auto& patch = submitted[0].actions().front().bindings.front();
        ASSERT_TRUE(patch.control);
        EXPECT_EQ(std::get<Input::Key>(*patch.control), Input::Key::K);
    }

    TEST_F(ProjectUiGpuTest, InvalidResourcesAndCandidateServiceCallsKeepTheLiveSession) {
        click("settings");
        auto* live = &document();
        auto image = original_document;
        image.insert(image.find("</body>"),
            "<img src=\"missing.png\" style=\"width:16px; height:16px;\" />");
        write("runtime.rml", image);
        EXPECT_FALSE(ui->reload());
        EXPECT_EQ(&document(), live);
        EXPECT_TRUE(ui->is_modal());
        EXPECT_EQ(loads, 1u);
        write("runtime.rml", original_document);
        write("runtime.ui.lua", R"lua(return {model={}, on_mount=function(self,ui)
            ui.input_begin({}, {})
        end})lua");
        const auto rejected = ui->reload();
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().find("before controller publication"), std::string::npos);
        EXPECT_EQ(loads, 1u);
        EXPECT_EQ(&document(), live);
        ASSERT_TRUE(submit_frame());
    }

    TEST_F(ProjectUiGpuTest, CandidateReadsSettingsWithoutPublishingHostMutations) {
        display_services = quality_services = audio_services = true;
        saved_display = {960, 540, WindowMode::Windowed, true};
        saved_quality.render_scale = 0.75f;
        saved_audio = {0.25f, 0.5f, 0.75f};
        write("other.rml", R"rml(<rml><head><style>
            body {font-family: Comet; font-weight: bold;}
            </style></head><body data-model="ui"><div id="readback">{{readback}}</div>
            </body></rml>)rml");
        const std::string before_mutation = R"lua(return {model={readback=""},
            on_mount=function(self,ui)
                assert(ui.display_settings().width == 960)
                assert(ui.quality_settings().render_scale == 0.75)
                local audio = ui.audio_settings()
                assert(audio.master_volume == 0.25 and audio.effects_volume == 0.5
                    and audio.music_volume == 0.75 and audio.defaults and audio.active)
                ui.set("readback", "ready")
        )lua";
        const auto controller = [&](const std::string& mutation) {
            return before_mutation + mutation + "\nend}";
        };
        write("other.ui.lua", controller(""));
        create_ui({}, {"ui/other.rml", "ui/other.ui.lua"});
        auto* live = &document();
        EXPECT_EQ(live->GetElementById("readback")->GetInnerRML(), "ready");
        for(const auto* mutation : {"ui.audio_apply(1, 1, 1)", "ui.quality_apply(1, 1, 1)",
                "ui.display_apply(800, 600, 'windowed', true)", "ui.display_confirm()",
                "ui.display_revert()", "ui.emit('character.next')"}) {
            SCOPED_TRACE(mutation);
            write("other.ui.lua", controller(mutation));
            const auto rejected = ui->reload();
            ASSERT_FALSE(rejected);
            EXPECT_NE(rejected.error().find("before controller publication"), std::string::npos);
            EXPECT_EQ(&document(), live);
            EXPECT_EQ(audio_applications, 0u);
            EXPECT_EQ(quality_applications, 0u);
            EXPECT_EQ(display_applications, 0u);
            EXPECT_EQ(saved_audio, (AudioSettings{0.25f, 0.5f, 0.75f}));
            ASSERT_TRUE(submit_frame());
        }
        write("other.ui.lua", controller(""));
        ASSERT_TRUE(ui->reload());
        EXPECT_EQ(document().GetElementById("readback")->GetInnerRML(), "ready");
    }

    TEST_F(ProjectUiGpuTest, SettingsReadErrorsRemainUsableAfterLuaAllocationFailure) {
        display_services = quality_services = audio_services = true;
        write("other.rml", R"rml(<rml><head></head><body data-model="ui"></body></rml>)rml");
        for(const auto* api : {"display_settings", "quality_settings", "audio_settings"}) {
            SCOPED_TRACE(api);
            settings_load_error = "Cannot read player settings";
            write("other.ui.lua", std::string(R"lua(return {model={},
                on_mount=function(self,ui)
                    local value, message = ui.)lua")
                                      + api + R"lua(()
                    assert(value == nil and message == "Cannot read player settings")
                end})lua");
            create_ui({}, {"ui/other.rml", "ui/other.ui.lua"});
            auto* live = &document();
            // 错误返回也会分配 Lua 字符串；超过 VM 预算后仍应保留可用的旧控制器。
            settings_load_error.assign(17 * 1024 * 1024, 'x');
            const auto rejected = ui->reload();
            ASSERT_FALSE(rejected);
            EXPECT_NE(rejected.error().find("memory"), std::string::npos);
            EXPECT_EQ(&document(), live);
            settings_load_error = "Cannot read player settings";
            ASSERT_TRUE(ui->reload());
            ASSERT_TRUE(submit_frame());
        }
    }

    TEST_F(ProjectUiGpuTest, FocusFirstSkipsControlsThatDeclineFocus) {
        write("other.rml", R"rml(<rml><head><style>
            body {font-family: Comet; font-weight: bold;}
            button {tab-index: auto; display: block; height: 30dp;}
            #decorative {focus: none;}
            </style></head><body data-model="ui"><div id="panel">
            <button id="decorative">Decoration</button><button id="disabled" disabled>Disabled</button>
            <button id="action" data-event-click="command('apply')">Apply</button>
            <div>{{result}}</div></div></body></rml>)rml");
        write("other.ui.lua", R"lua(return {model={result=""},
            on_mount=function(self,ui) ui.modal(true) end,
            on_present=function(self,ui)
                if not ui.has_focus("panel") then ui.focus_first("panel") end
            end,
            on_event=function(self,ui,operation)
                if operation == "apply" then ui.set("result", "activated") end
            end})lua");
        create_ui({}, {"ui/other.rml", "ui/other.ui.lua"});
        auto& context = *Rml::GetContext(0);
        EXPECT_EQ(context.GetFocusElement(), document().GetElementById("action"));
        queue_key(GLFW_KEY_ENTER);
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(document().GetInnerRML().find("activated"), std::string::npos);
        context.GetFocusElement()->Blur();
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(context.GetFocusElement(), document().GetElementById("action"));
    }

    TEST_F(ProjectUiGpuTest, QueuedStructuralEditsDiagnoseRemovedTargets) {
        write("other.rml",
            R"rml(<rml><head><style>body {font-family: Comet; font-weight: bold;}</style></head>
            <body data-model="ui"><div id="a-parent"><div id="b-child"></div></div>
            <button id="edit" data-event-click="command('edit')">Edit</button></body></rml>)rml");
        write("other.ui.lua", R"lua(return {model={},on_event=function(self,ui)
            ui.markup("a-parent", "Changed")
            ui.markup("b-child", "Removed")
        end})lua");
        create_ui({}, {"ui/other.rml", "ui/other.ui.lua"});
        document().GetElementById("edit")->Click();
        const auto result = ui->frame(engine->get_window().publish_input_frame(), {.fps = 60});
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("b-child"), std::string::npos);
    }

}
