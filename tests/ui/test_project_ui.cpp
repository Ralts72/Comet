#include "ui/project_ui.h"
#include "support/engine_fixture.h"
#include "support/temporary_directory.h"
#include "core/window.h"
#include "render/overlay_record_context.h"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>

#include <array>
#include <fstream>
#include <string>

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
        bool display_services = false;
        bool fail_display_save = false;
        bool preview = false;
        std::optional<bool> vsync_active;
        DisplaySettings saved_display;
        unsigned display_applications = 0;
        bool quality_services = false, fail_quality_save = false;
        QualitySettings saved_quality;
        unsigned quality_applications = 0;
        bool audio_services = false, fail_audio_save = false;
        AudioSettings saved_audio;
        unsigned audio_applications = 0;

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
                    return Result<DisplaySettings>::success(saved_display);
                };
                services.apply_display = [this](DisplaySettings value) {
                    if(fail_display_save)
                        return Result<void>::failure("Simulated display save failure");
                    saved_display = value;
                    ++display_applications;
                    return Result<void>::success();
                };
            }
            if(quality_services) {
                services.load_quality = [this] {
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
            engine->get_renderer().set_overlay(
                {.render = [this](OverlayRecordContext& frame) { return ui->render(frame); },
                    .release = [this] { ui->release_swapchain_resources(); },
                    .rebuild =
                        [this](const SwapchainCompatibility& compatibility) {
                            return ui->rebuild_swapchain_resources(compatibility);
                        }});
            ASSERT_TRUE(submit_frame());
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
            ASSERT_TRUE(submit_frame());
        }
        Result<void, GraphicsError> submit_frame() {
            auto& window = engine->get_window();
            auto& renderer = engine->get_renderer();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                window.poll_events();
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
            auto* window = engine->get_window().get();
            const auto callback = glfwSetKeyCallback(window, nullptr);
            glfwSetKeyCallback(window, callback);
            ASSERT_NE(callback, nullptr);
            callback(window, key, 0, GLFW_PRESS, 0);
            callback(window, key, 0, GLFW_RELEASE, 0);
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
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(engine->get_renderer().get_quality_settings().render_scale, 0.5f);
        EXPECT_NE(document().GetElementById("quality-active")->GetInnerRML().find("50%"),
            std::string::npos);
        EXPECT_EQ(engine->get_window().get_size(), window_size);
        EXPECT_TRUE(ui->is_modal());
        const auto& samples = engine->get_renderer().supported_msaa_samples();
        EXPECT_NE(std::ranges::find(samples, saved_quality.msaa_samples), samples.end());
    }

    TEST_F(ProjectUiGpuTest, DisplayDraftCancelsAndSaveFailurePreservesItAcrossReload) {
        display_services = true;
        create_ui(InputActions{});
        click("settings");
        click("display-size");
        click("display-vsync");
        click("cancel");
        EXPECT_EQ(display_applications, 0U);
        EXPECT_EQ(saved_display, DisplaySettings{});
        click("settings");
        click("display-size");
        click("display-vsync");
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
        EXPECT_EQ(saved_display, (DisplaySettings{1280, 720, WindowMode::Windowed, true}));
        EXPECT_TRUE(ui->is_modal());
    }

    TEST_F(ProjectUiGpuTest, PreviewDisplayControlsKeepStandaloneModeAndVsync) {
        display_services = true;
        preview = true;
        saved_display.mode = WindowMode::Fullscreen;
        create_ui(InputActions{});
        const auto editor_size = engine->get_window().get_size();
        click("settings");
        EXPECT_TRUE(document().GetElementById("display-mode")->HasAttribute("disabled"));
        EXPECT_TRUE(document().GetElementById("display-vsync")->HasAttribute("disabled"));
        click("display-restore");
        click("display-apply");
        EXPECT_EQ(saved_display.width, 1280);
        EXPECT_EQ(saved_display.mode, WindowMode::Fullscreen);
        EXPECT_FALSE(saved_display.vsync);
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
        const auto cursor = glfwSetCursorPosCallback(window.get(), nullptr);
        glfwSetCursorPosCallback(window.get(), cursor);
        ASSERT_NE(cursor, nullptr);
        cursor(window.get(), (offset.x + size.x / 2) * window_size.x / dimensions.x,
            (offset.y + size.y / 2) * window_size.y / dimensions.y);
        const auto mouse = glfwSetMouseButtonCallback(window.get(), nullptr);
        glfwSetMouseButtonCallback(window.get(), mouse);
        ASSERT_NE(mouse, nullptr);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ASSERT_TRUE(submit_frame());
        EXPECT_EQ(Rml::GetContext(0)->GetFocusElement(), previous);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ASSERT_TRUE(submit_frame());
        EXPECT_NE(document().GetElementById("action-name")->GetInnerRML().find("palette.confirm"),
            std::string::npos);
    }

    TEST_F(ProjectUiGpuTest, CandidateFailuresPreservePageControllerAndInputDraft) {
        click("settings");
        ASSERT_TRUE(ui->is_modal());
        const auto project = Project::load(std::filesystem::path(PROJECT_ROOT_DIR) / "demo");
        ASSERT_TRUE(project);
        const auto& action = project.value().input_actions().actions().front();
        click("toggle_binding-" + action.bindings.front().id.to_string());
        auto* live = &document();
        for(const auto& script :
            {std::string("return {on_mount=42,model={}}"), std::string("while true do end"),
                original_controller + "\nerror('candidate failed')"}) {
            write("runtime.ui.lua", script);
            EXPECT_FALSE(ui->reload());
            EXPECT_EQ(&document(), live);
            ASSERT_TRUE(submit_frame());
            EXPECT_TRUE(ui->is_modal());
        }
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
        ASSERT_TRUE(ui->reload());
        ASSERT_TRUE(submit_frame());
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
        queue_key(GLFW_KEY_ENTER);
        queue_key(GLFW_KEY_TAB);
        queue_key(GLFW_KEY_ENTER);
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
