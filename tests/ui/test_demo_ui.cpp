#include "input/player_input_edit.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    class CpuRenderer final: public Rml::RenderInterface {
    public:
        Rml::CompiledGeometryHandle CompileGeometry(const Rml::Span<const Rml::Vertex> vertices,
            const Rml::Span<const int> indices) override {
            for(const auto index : indices)
                EXPECT_TRUE(index >= 0 && static_cast<std::size_t>(index) < vertices.size());
            const auto handle = ++m_next;
            geometry.emplace(handle, vertices.size());
            ++compiled;
            return handle;
        }
        void RenderGeometry(const Rml::CompiledGeometryHandle handle, Rml::Vector2f,
            const Rml::TextureHandle texture) override {
            EXPECT_TRUE(geometry.contains(handle));
            EXPECT_TRUE(texture == 0 || textures.contains(texture));
            ++draws;
        }
        void ReleaseGeometry(const Rml::CompiledGeometryHandle handle) override {
            EXPECT_EQ(geometry.erase(handle), 1u);
        }
        Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
        Rml::TextureHandle GenerateTexture(
            const Rml::Span<const Rml::byte> source, const Rml::Vector2i size) override {
            EXPECT_GT(size.x, 0);
            EXPECT_GT(size.y, 0);
            EXPECT_EQ(source.size(), static_cast<std::size_t>(size.x) * size.y * 4);
            const auto handle = ++m_next;
            textures.emplace(handle, source.size());
            atlas_bytes += source.size();
            return handle;
        }
        void ReleaseTexture(const Rml::TextureHandle handle) override {
            EXPECT_EQ(textures.erase(handle), 1u);
        }
        void EnableScissorRegion(bool) override {}
        void SetScissorRegion(Rml::Rectanglei) override {}
        void SetTransform(const Rml::Matrix4f*) override {}
        void EnableClipMask(const bool enabled) override { unsupported |= enabled; }
        void RenderToClipMask(
            Rml::ClipMaskOperation, Rml::CompiledGeometryHandle, Rml::Vector2f) override {
            unsupported = true;
        }
        Rml::LayerHandle PushLayer() override {
            unsupported = true;
            return 0;
        }

        std::unordered_map<std::uintptr_t, std::size_t> geometry;
        std::unordered_map<std::uintptr_t, std::size_t> textures;
        std::size_t atlas_bytes = 0;
        std::size_t compiled = 0;
        std::size_t draws = 0;
        bool unsupported = false;

    private:
        std::uintptr_t m_next = 0;
    };

    class CpuSystem final: public Rml::SystemInterface {
    public:
        double GetElapsedTime() override { return 1; }
        bool LogMessage(const Rml::Log::Type type, const Rml::String& message) override {
            if(type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT
                || type == Rml::Log::LT_WARNING)
                diagnostics.push_back(message);
            return true;
        }
        std::vector<std::string> diagnostics;
    };

    class DemoUiDocumentTest: public testing::Test {
    protected:
        void SetUp() override {
            Rml::SetSystemInterface(&system);
            Rml::SetRenderInterface(&renderer);
            ASSERT_TRUE(Rml::Initialise());
            initialized = true;
            const std::filesystem::path fonts(COMET_TEST_UI_FONT_DIRECTORY);
            ASSERT_TRUE(Rml::LoadFontFace((fonts / "Roboto-Bold.ttf").string(), "Comet",
                Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold));
            ASSERT_TRUE(Rml::LoadFontFace((fonts / "NotoSansSC-Bold.otf").string(), "CometChinese",
                Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold, true));
            context = Rml::CreateContext("runtime-ui-test", {1280, 720});
            ASSERT_NE(context, nullptr);
            auto constructor = context->CreateDataModel("ui");
            ASSERT_TRUE(constructor);
            ASSERT_TRUE(constructor.Bind("fps_text", &fps));
            ASSERT_TRUE(constructor.Bind("action_name", &action));
            ASSERT_TRUE(constructor.Bind("status_text", &status));
            ASSERT_TRUE(constructor.Bind("error_text", &error));
            ASSERT_TRUE(constructor.Bind("menu_available", &available));
            ASSERT_TRUE(constructor.Bind("waiting", &waiting));
            ASSERT_TRUE(constructor.Bind("has_actions", &has_actions));
            for(const auto* name : {"display_available", "display_preview", "display_waiting",
                    "quality_available", "audio_available"})
                ASSERT_TRUE(constructor.Bind(name, &display_flag));
            for(const auto* name :
                {"display_size", "display_mode", "display_vsync", "display_status", "display_error",
                    "display_width", "display_height", "display_active_vsync", "quality_msaa",
                    "quality_anisotropy", "quality_scale", "quality_active", "quality_status",
                    "quality_error", "quality_preset", "audio_active", "audio_error"})
                ASSERT_TRUE(constructor.Bind(name, &display_text));
            for(const auto* name : {"audio_master", "audio_effects", "audio_music"})
                ASSERT_TRUE(constructor.Bind(name, &audio_volume));
            ASSERT_TRUE(constructor.BindEventCallback(
                "command", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
                    commands.push_back(args[0].Get<Rml::String>());
                    if(commands.back() == "apply")
                        edit.apply();
                    else if(commands.back() == "restore")
                        edit.restore_all();
                }));
            model = constructor.GetModelHandle();
            document = context->LoadDocument(
                (std::filesystem::path(COMET_TEST_UI_DIRECTORY) / "runtime.rml").string());
            ASSERT_NE(document, nullptr);
            document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            ASSERT_TRUE(context->Update());
            commands.clear();
        }
        void TearDown() override {
            if(initialized) {
                if(context)
                    Rml::RemoveContext("runtime-ui-test");
                Rml::Shutdown();
                Rml::SetRenderInterface(nullptr);
                Rml::SetSystemInterface(nullptr);
            }
            EXPECT_TRUE(renderer.geometry.empty());
            EXPECT_TRUE(renderer.textures.empty());
        }
        void show_menu() {
            document->GetElementById("menu")->SetProperty("display", "block");
            ASSERT_TRUE(context->Update());
        }

        CpuSystem system;
        CpuRenderer renderer;
        Rml::Context* context = nullptr;
        Rml::ElementDocument* document = nullptr;
        Rml::DataModelHandle model;
        Comet::PlayerInputEdit edit;
        std::vector<std::string> commands;
        Rml::String fps = "60 FPS";
        Rml::String action = "跳跃";
        Rml::String status = "更改会在应用后保存。";
        Rml::String error;
        bool available = true;
        bool waiting = false;
        bool has_actions = true;
        bool display_flag = false;
        float audio_volume = 100;
        std::string display_text;
        bool initialized = false;
    };
}

TEST_F(DemoUiDocumentTest, AudioSliderHasVisibleGeometryAndSupportsKeyboardChanges) {
    display_flag = true;
    model.DirtyVariable("audio_available");
    show_menu();
    auto* slider = document->GetElementById("audio-master");
    ASSERT_NE(slider, nullptr);
    EXPECT_GT(slider->GetClientWidth(), 0);
    Rml::Element* bar = nullptr;
    for(int index = 0; index < slider->GetNumChildren(true); ++index) {
        auto* child = slider->GetChild(index);
        if(child->GetTagName() == "sliderbar")
            bar = child;
    }
    ASSERT_NE(bar, nullptr);
    EXPECT_GT(bar->GetClientWidth(), 0);
    ASSERT_TRUE(slider->Focus(true));
    context->ProcessKeyDown(Rml::Input::KI_LEFT, 0);
    context->ProcessKeyUp(Rml::Input::KI_LEFT, 0);
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.back(), "audio_master");
    EXPECT_EQ(slider->GetAttribute("value", 0.0f), 99);
    ASSERT_TRUE(context->Render());
    EXPECT_FALSE(renderer.unsupported);
    EXPECT_TRUE(system.diagnostics.empty());
}

TEST_F(DemoUiDocumentTest, WaitingDisablesControlsAndFailureRestoresKeyboardFocus) {
    show_menu();
    auto* apply = document->GetElementById("apply");
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(apply->HasAttribute("disabled"));
    ASSERT_TRUE(apply->Focus(true));

    waiting = true;
    model.DirtyVariable("waiting");
    ASSERT_TRUE(context->Update());
    for(const auto* id : {"previous", "next", "restore", "cancel", "apply"}) {
        auto* control = document->GetElementById(id);
        ASSERT_NE(control, nullptr);
        EXPECT_TRUE(control->HasAttribute("disabled")) << id;
        EXPECT_EQ(control->GetComputedValues().tab_index(), Rml::Style::TabIndex::None) << id;
        EXPECT_FALSE(control->Focus(true)) << id;
    }

    waiting = false;
    error = "保存失败，请重试。";
    model.DirtyVariable("waiting");
    model.DirtyVariable("error_text");
    ASSERT_TRUE(context->Update());
    EXPECT_FALSE(apply->HasAttribute("disabled"));
    ASSERT_TRUE(apply->Focus(true));
    EXPECT_EQ(context->GetFocusElement(), apply);

    available = false;
    model.DirtyVariable("menu_available");
    ASSERT_TRUE(context->Update());
    auto* settings = document->GetElementById("settings");
    EXPECT_TRUE(settings->HasAttribute("disabled"));
    EXPECT_FALSE(settings->Focus(true));

    has_actions = false;
    model.DirtyVariable("has_actions");
    ASSERT_TRUE(context->Update());
    EXPECT_TRUE(apply->HasAttribute("disabled"));
    auto* cancel = document->GetElementById("cancel");
    EXPECT_FALSE(cancel->HasAttribute("disabled"));
    ASSERT_TRUE(cancel->Focus(true));
    EXPECT_EQ(context->GetFocusElement(), cancel);
}

TEST_F(DemoUiDocumentTest, SharedChineseFontsAndRoundedButtonsUseOnlyBasicRenderer) {
    for(const auto* id : {"hud", "fps", "settings", "notice", "menu", "panel", "action-selector",
            "action-name", "previous", "next", "bindings", "status", "error", "footer", "restore",
            "cancel", "apply"})
        ASSERT_NE(document->GetElementById(id), nullptr) << id;
    show_menu();
    ASSERT_TRUE(context->Render());
    EXPECT_GT(renderer.draws, 0u);
    EXPECT_GT(renderer.atlas_bytes, 0u);
    EXPECT_FALSE(renderer.unsupported);
    EXPECT_TRUE(system.diagnostics.empty())
        << (system.diagnostics.empty() ? "" : system.diagnostics.front());
    const auto compiled = renderer.compiled;
    ASSERT_TRUE(context->Update());
    ASSERT_TRUE(context->Render());
    EXPECT_EQ(renderer.compiled, compiled);
}

TEST_F(DemoUiDocumentTest, ResizeAndDensityUseFramebufferPixelsOnce) {
    show_menu();
    context->SetDimensions({640, 480});
    context->SetDensityIndependentPixelRatio(1);
    ASSERT_TRUE(context->Update());
    const auto width = document->GetElementById("panel")->GetClientWidth();
    EXPECT_GT(width, 0);
    EXPECT_LT(width, 640);
    context->SetDimensions({1280, 960});
    context->SetDensityIndependentPixelRatio(2);
    ASSERT_TRUE(context->Update());
    EXPECT_NEAR(document->GetElementById("panel")->GetClientWidth(), width * 2, 2);
    ASSERT_TRUE(context->Render());
    EXPECT_FALSE(renderer.unsupported);
}

TEST_F(DemoUiDocumentTest, KeyboardNavigationAndDynamicBindingCallbacksAreLive) {
    show_menu();
    document->GetElementById("bindings")
        ->SetInnerRML(
            "<button id=\"binding\" data-event-click=\"command('key','00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000002')\">录入按键</button>");
    ASSERT_TRUE(context->Update());
    auto* capture = document->GetElementById("binding");
    ASSERT_NE(capture, nullptr);
    ASSERT_TRUE(capture->Focus(true));
    context->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
    context->ProcessKeyUp(Rml::Input::KI_RETURN, 0);
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands.back(), "key");
    context->ProcessKeyDown(Rml::Input::KI_DOWN, 0);
    context->ProcessKeyUp(Rml::Input::KI_DOWN, 0);
    EXPECT_NE(context->GetFocusElement(), capture);
    context->ProcessKeyDown(Rml::Input::KI_TAB, 0);
    context->ProcessKeyUp(Rml::Input::KI_TAB, 0);
    EXPECT_NE(context->GetFocusElement(), nullptr);
    EXPECT_TRUE(system.diagnostics.empty());
}

TEST_F(DemoUiDocumentTest, ApplyPreservesSparseRecordsAndSaveFailureCanRetry) {
    const auto action_id = Comet::Uuid::generate();
    const auto binding_id = Comet::Uuid::generate();
    const auto stale_action = Comet::Uuid::generate();
    const auto stale_binding = Comet::Uuid::generate();
    auto defaults = Comet::InputActions::create({{"Jump", Comet::InputActions::Type::Button,
        {{Comet::Input::Key::Space, 1, 0, binding_id}}, {}, action_id}});
    ASSERT_TRUE(defaults);
    auto current = Comet::InputOverrides::create({{stale_action, Comet::InputActions::Type::Button,
        true, {{.id = stale_binding, .control = Comet::Input::Key::A, .disabled = true}}}});
    ASSERT_TRUE(current);
    edit.reset(defaults.value(), current.value());
    edit.change_control(action_id, binding_id, Comet::Input::Key::B);
    const auto draft = edit.draft();
    show_menu();
    document->GetElementById("apply")->Click();
    auto request = edit.take_request();
    ASSERT_TRUE(request);
    EXPECT_EQ(*request, draft);
    EXPECT_FALSE(edit.complete(Comet::Result<void>::failure("disk is read only")));
    EXPECT_EQ(edit.draft(), draft);
    document->GetElementById("apply")->Click();
    request = edit.take_request();
    ASSERT_TRUE(request);
    EXPECT_EQ(*request, draft);
    EXPECT_TRUE(edit.complete(Comet::Result<void>::success()));
}

TEST_F(DemoUiDocumentTest, HudBindingChangesWithoutReplacingDocument) {
    fps = "144 FPS";
    model.DirtyVariable("fps_text");
    ASSERT_TRUE(context->Update());
    EXPECT_EQ(document->GetElementById("fps")->GetInnerRML(), "144 FPS");
    error = "模板加载失败，保留旧界面。";
    model.DirtyVariable("error_text");
    ASSERT_TRUE(context->Update());
    EXPECT_EQ(document->GetElementById("notice")->GetInnerRML(), error);
}
