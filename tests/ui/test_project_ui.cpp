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
            auto created = Ui::ProjectUi::create(engine->get_window(), engine->get_renderer(),
                entry,
                {.resource_root = documents.path(), .font_directory = COMET_TEST_UI_FONT_DIRECTORY},
                {.input_actions = actions,
                    .load_input =
                        [this] {
                            ++loads;
                            return Result<InputOverrides>::success(saved);
                        },
                    .apply_input =
                        [this](InputOverrides value) {
                            submitted.push_back(value);
                            if(fail_save)
                                return Result<void>::failure("Simulated save failure");
                            saved = std::move(value);
                            return Result<void>::success();
                        }});
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
                auto result = ui->frame(window.publish_input_frame(), {.fps = 60});
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
    };
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
