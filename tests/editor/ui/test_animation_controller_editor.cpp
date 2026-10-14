#ifdef COMET_TEST_EDITOR_UI
#include "animation/animation_controller_editor.h"
#include "inspector/animation_preview.h"
#include "support/imgui_context.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <string_view>

namespace CometEditor::Tests {
    class AnimationControllerEditorUiTest: public testing::Test {
    protected:
        Comet::Tests::ImGuiTestContext context{{1400, 950}};
        Comet::AnimationController data;
        Comet::AnimationController saved;
        AnimationControllerEditor editor;
        AnimationPreview preview;
        AnimationControllerEditor::Model source;

        void SetUp() override {
            data.initial = "ground";
            data.parameters = {{"speed", 0.f}, {"jump", false}};
            data.states = {{"ground", {{0, "idle"}, {4, "run"}}, "speed"},
                {"jump", {{0, "jump"}}, {}, 0, false}};
            data.transitions = {
                {"ground", "jump", .1f, false,
                    {{"jump", Comet::AnimationController::Condition::Compare::Equal, true}}},
                {"jump", "ground", .1f, true}};
            saved = data;
            auto animation = std::make_shared<Comet::AnimationData>();
            animation->clips = {{"idle", 1}, {"run", 2}, {"jump", 1}};
            source = {Comet::EntityUuid::generate(), "角色", Comet::AssetHandle{42}, animation};
            editor.reset(data);
            editor.set_models({source}, Comet::AssetHandle{42}, false);
            preview.bind({1, source.entity}, animation, "idle");
            frame();
            frame();
        }

        void frame() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({960, 20});
            ImGui::SetNextWindowSize({410, 870});
            ImGui::Begin("Controller properties");
            editor.render_details(data);
            ImGui::End();
            ImGui::SetNextWindowPos({10, 20});
            ImGui::SetNextWindowSize({930, 870});
            static_cast<void>(editor.render_window(data, data != saved, &preview));
            ImGui::Render();
        }

        ImGuiWindow* child(std::string_view prefix) {
            for(auto* window : ImGui::GetCurrentContext()->Windows)
                if(std::string_view(window->Name).starts_with(prefix))
                    return window;
            ADD_FAILURE() << "Missing child window: " << prefix;
            return nullptr;
        }

        void activate(const char* window_name, const char* label) {
            auto* window = ImGui::FindWindowByName(window_name);
            ASSERT_NE(window, nullptr);
            ImGui::ActivateItemByID(window->GetID(label));
            frame();
            frame();
        }

        void click_node(std::size_t index) {
            auto* graph = child("动画控制器/State graph");
            ASSERT_NE(graph, nullptr);
            const ImVec2 point{graph->DC.CursorStartPos.x + 35 + float(index % 3) * 250 + 80,
                graph->DC.CursorStartPos.y + 40 + 125 + float(index / 3) * 160 + 25};
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        }
    };

    TEST_F(
        AnimationControllerEditorUiTest, CreatesConnectionsAndDeletesStateReferencesWithUndoRedo) {
        activate("动画控制器", "添加状态");
        ASSERT_EQ(data.states.size(), 3u);
        EXPECT_EQ(data.states.back().name, "state1");
        activate("动画控制器", "连接状态");
        click_node(0);
        ASSERT_EQ(data.transitions.size(), 3u);
        EXPECT_EQ(data.transitions.back().from, "state1");
        EXPECT_EQ(data.transitions.back().to, "ground");
        activate("Controller properties", "提高优先级");
        EXPECT_EQ(data.transitions[1].from, "state1");
        click_node(0);
        activate("Controller properties", "删除状态及关联切换");
        ASSERT_EQ(data.states.size(), 2u);
        EXPECT_EQ(data.initial, "jump");
        EXPECT_TRUE(data.transitions.empty());
        activate("动画控制器", "###Undo Controller");
        ASSERT_EQ(data.states.size(), 3u);
        EXPECT_EQ(data.initial, "ground");
        ASSERT_EQ(data.transitions.size(), 3u);
        activate("动画控制器", "###Redo Controller");
        EXPECT_EQ(data.states.size(), 2u);
        EXPECT_TRUE(data.transitions.empty());
        EXPECT_TRUE(data.validate());
    }

    TEST_F(AnimationControllerEditorUiTest, ModelClipsAndPreviewValuesDoNotChangeAssetDefaults) {
        auto* parameters = child("动画控制器/Parameters");
        ASSERT_NE(parameters, nullptr);
        const auto group = ImHashStr("jump", 0, parameters->ID);
        ImGui::ActivateItemByID(ImHashStr("##Preview value", 0, group));
        frame();
        frame();
        EXPECT_EQ(data, saved);
        activate("动画控制器", "###Play Controller Preview");
        ASSERT_TRUE(preview.playing());
        preview.advance(.25);
        const auto captured = preview.snapshot();
        ASSERT_TRUE(captured);
        EXPECT_EQ(captured->current.clip, "idle");
        activate("动画控制器", "###Pause Controller Preview");
        preview.advance(.5);
        EXPECT_EQ(preview.snapshot(), captured);
        activate("动画控制器", "###Stop Controller Preview");
        EXPECT_FALSE(preview.snapshot());
        EXPECT_EQ(data, saved);
    }

    TEST_F(
        AnimationControllerEditorUiTest, PlayUsesReadOnlyInstanceFeedbackAndNeverPublishesPreview) {
        source.playback = std::make_shared<const Comet::AnimationPlayback>(
            Comet::AnimationPlayback{.current = {.clip = "jump", .time = .2}, .state = "jump"});
        source.parameters = {{"speed", 3.f}, {"jump", true}};
        editor.set_models({source}, Comet::AssetHandle{42}, true);
        preview.stop();
        frame();
        EXPECT_FALSE(preview.snapshot());
        EXPECT_EQ(editor.model()->playback, source.playback);
        EXPECT_EQ(data, saved);
    }
    TEST_F(AnimationControllerEditorUiTest, EditsTimelineMarkersWithControllerUndoAndRedo) {
        activate("Controller properties", "添加事件");
        ASSERT_EQ(data.states[0].events.size(), 1u);
        EXPECT_EQ(data.states[0].events[0].phase, .5f);
        EXPECT_EQ(data.states[0].events[0].name, "event");
        EXPECT_NE(data, saved);
        activate("Controller properties", "删除事件");
        EXPECT_TRUE(data.states[0].events.empty());
        EXPECT_TRUE(editor.undo(data));
        ASSERT_EQ(data.states[0].events.size(), 1u);
        EXPECT_TRUE(editor.redo(data));
        EXPECT_TRUE(data.states[0].events.empty());
    }
}
#endif
