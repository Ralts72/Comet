#include "player_input_menu.h"
#include "support/engine_fixture.h"
#include "support/temporary_directory.h"
#include "core/window.h"
#include "core/project.h"
#include "render/overlay_record_context.h"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>

#include <array>
#include <fstream>
#include <string>

namespace Comet::Tests {
    class PlayerInputMenuGpuTest: public EngineTest {
    protected:
        TemporaryDirectory documents;
        std::unique_ptr<CometApp::PlayerInputMenu> ui;
        std::string original_document;
        unsigned rendered_frames = 0;
        bool input_blocked = false;

        void SetUp() override {
            EngineTest::SetUp();
            ASSERT_TRUE(engine);
            std::filesystem::create_directories(documents.path() / "ui");
            for(const auto* filename : {"runtime.rml", "runtime.rcss"}) {
                std::filesystem::copy_file(
                    std::filesystem::path(COMET_TEST_UI_DIRECTORY) / filename,
                    documents.path() / "ui" / filename);
            }
            std::ifstream file(documents.path() / "ui" / "runtime.rml");
            ASSERT_TRUE(file);
            original_document.assign(
                std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            auto created =
                CometApp::PlayerInputMenu::create(engine->get_window(), engine->get_renderer(),
                    {.resource_root = documents.path(),
                        .font_directory = COMET_TEST_UI_FONT_DIRECTORY});
            ASSERT_TRUE(created) << created.error();
            ui = std::move(created).value();
            engine->get_renderer().set_overlay({.render =
                                                    [this](OverlayRecordContext& frame) {
                                                        auto result = ui->render(frame);
                                                        if(result)
                                                            ++rendered_frames;
                                                        return result;
                                                    },
                .release = [this] { ui->release_swapchain_resources(); },
                .rebuild =
                    [this](const SwapchainCompatibility& compatibility) {
                        return ui->rebuild_swapchain_resources(compatibility);
                    }});
        }

        void TearDown() override {
            if(engine) {
                engine->get_renderer().set_overlay({});
                engine->get_renderer().wait_idle();
            }
            ui.reset();
            EngineTest::TearDown();
        }

        void write_document(const std::string& contents) {
            std::ofstream file(documents.path() / "ui" / "runtime.rml", std::ios::trunc);
            ASSERT_TRUE(file);
            file << contents;
            file.flush();
            ASSERT_TRUE(file);
        }

        std::string with_content(const std::string& contents) const {
            auto candidate = original_document;
            candidate.insert(candidate.find("</body>"), contents);
            return candidate;
        }

        Result<void, GraphicsError> submit_frame() {
            auto& window = engine->get_window();
            auto& renderer = engine->get_renderer();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                window.poll_events();
                input_blocked = ui->frame(window.publish_input_frame(), {.fps = 60});
                auto prepared = renderer.prepare_frame();
                if(!prepared)
                    return Result<void, GraphicsError>::failure(prepared.error());
                if(prepared.value() == Renderer::FramePreparation::Ready)
                    return renderer.render_frame();
            }
            return Result<void, GraphicsError>::failure(
                {"Player input menu presentation remained deferred"});
        }

        void queue_key(const int key) {
            auto* window = engine->get_window().get();
            const auto callback = glfwSetKeyCallback(window, nullptr);
            glfwSetKeyCallback(window, callback);
            ASSERT_NE(callback, nullptr);
            callback(window, key, 0, GLFW_PRESS, 0);
            callback(window, key, 0, GLFW_RELEASE, 0);
        }
    };

    TEST_F(PlayerInputMenuGpuTest, DemoMenuLayoutFitsAfterOpeningFromSettings) {
        auto project = Project::load(std::filesystem::path(PROJECT_ROOT_DIR) / "demo");
        ASSERT_TRUE(project) << project.error();
        auto* context = Rml::GetContext(0);
        ASSERT_NE(context, nullptr);
        auto* document = context->GetDocument(0);
        ASSERT_NE(document, nullptr);
        document->GetElementById("settings")->Click();
        ASSERT_TRUE(ui->take_open_request());
        ui->open(project.value().input_actions(), {});
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

    TEST_F(PlayerInputMenuGpuTest, FailedCandidateReloadsKeepTheLiveDocumentRenderable) {
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(ui->is_open());
        ui->open({}, {});
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(input_blocked);

        auto missing_element = original_document;
        const auto id = missing_element.find("id=\"apply\"");
        ASSERT_NE(id, std::string::npos);
        missing_element.replace(id, std::strlen("id=\"apply\""), "id=\"missing-apply\"");
        write_document(missing_element);
        const auto missing_id = ui->reload_documents();
        ASSERT_FALSE(missing_id);
        EXPECT_NE(missing_id.error().find("apply"), std::string::npos);
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_open());
        EXPECT_TRUE(input_blocked);

        write_document(with_content(
            "<img src=\"missing-image.png\" style=\"position: absolute; top: 0px; left: 0px; "
            "width: 16px; height: 16px;\" />"));
        const auto missing_image = ui->reload_documents();
        ASSERT_FALSE(missing_image);
        EXPECT_FALSE(missing_image.error().empty());
        EXPECT_NE(missing_image.error().find("missing-image.png"), std::string::npos);
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_open());

        write_document(with_content(
            "<div style=\"position: absolute; top: 0px; left: 0px; width: 16px; height: 16px; "
            "background-color: #fff; filter: blur(2px);\"></div>"));
        const auto unsupported_effect = ui->reload_documents();
        ASSERT_FALSE(unsupported_effect);
        EXPECT_NE(unsupported_effect.error().find("not supported"), std::string::npos);
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_open());

        write_document(original_document);
        const auto restored = ui->reload_documents();
        ASSERT_TRUE(restored) << restored.error();
        ASSERT_TRUE(submit_frame());
        ui->close();
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(ui->is_open());
        EXPECT_EQ(rendered_frames, 7u);
    }

    TEST_F(PlayerInputMenuGpuTest, AsyncApplyFailureKeepsDraftAndDiscardsWaitingCloseRequests) {
        Uuid::Bytes bytes{};
        bytes.back() = 1;
        const Uuid action_id(bytes);
        bytes.back() = 2;
        const Uuid binding_id(bytes);
        const auto defaults = InputActions::create({{"jump", InputActions::Type::Button,
            {{Input::Key::Space, 1, 0, binding_id}}, "", action_id}});
        ASSERT_TRUE(defaults);
        const auto current = InputOverrides::create({{action_id, InputActions::Type::Button, false,
            {{.id = binding_id, .control = Input::Key::J}}}});
        ASSERT_TRUE(current);

        // 固定 Tab 路径为“录入按键→应用”，保留生产文档的全部控件和绑定。
        auto navigation_document = original_document;
        navigation_document.insert(navigation_document.find("</head>"),
            "<style>button { tab-index: none; } #key-" + binding_id.to_string()
                + ", #apply { tab-index: auto; }</style>");
        write_document(navigation_document);
        ASSERT_TRUE(ui->reload_documents());

        auto& window = engine->get_window();
        glfwFocusWindow(window.get());
        window.poll_events();
        const auto focus = glfwSetWindowFocusCallback(window.get(), nullptr);
        glfwSetWindowFocusCallback(window.get(), focus);
        ASSERT_NE(focus, nullptr);
        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        window.discard_pending_input();
        ui->open(defaults.value(), current.value());
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(input_blocked);

        auto hostile_candidate = navigation_document;
        const auto body = hostile_candidate.find("<body data-model=\"runtime\">");
        ASSERT_NE(body, std::string::npos);
        hostile_candidate.replace(body, std::strlen("<body data-model=\"runtime\">"),
            "<body data-model=\"runtime\" data-event-load=\"command('restore')\">");
        const auto apply_id = hostile_candidate.find("id=\"apply\"");
        ASSERT_NE(apply_id, std::string::npos);
        hostile_candidate.replace(apply_id, std::strlen("id=\"apply\""), "id=\"missing-apply\"");
        write_document(hostile_candidate);
        const auto rejected = ui->reload_documents();
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().find("apply"), std::string::npos);
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_open());

        queue_key(GLFW_KEY_TAB);
        queue_key(GLFW_KEY_ENTER);
        ASSERT_TRUE(submit_frame());
        const auto first_request = ui->take_request();
        ASSERT_TRUE(first_request);
        EXPECT_EQ(*first_request, current.value());
        EXPECT_FALSE(ui->take_request());

        queue_key(GLFW_KEY_ESCAPE);
        ASSERT_TRUE(submit_frame());
        ui->close();
        EXPECT_TRUE(ui->is_open());
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(input_blocked);
        EXPECT_TRUE(ui->is_open());
        ui->complete(Result<void>::failure("Simulated asynchronous save failure"));
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(input_blocked);
        EXPECT_TRUE(ui->is_open());

        // 重新取得窗口输入后恢复首个绑定焦点，再通过正常导航提交。
        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        ASSERT_TRUE(submit_frame());
        EXPECT_TRUE(ui->is_open());
        queue_key(GLFW_KEY_TAB);
        queue_key(GLFW_KEY_ENTER);
        ASSERT_TRUE(submit_frame());
        const auto retry = ui->take_request();
        ASSERT_TRUE(retry);
        EXPECT_EQ(*retry, *first_request);
        ui->complete(Result<void>::success());
        EXPECT_FALSE(ui->is_open());
        EXPECT_TRUE(input_blocked);
        ASSERT_TRUE(submit_frame());
        EXPECT_FALSE(input_blocked);
        EXPECT_FALSE(ui->take_request());
    }
}
