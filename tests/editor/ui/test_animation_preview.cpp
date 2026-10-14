#ifdef COMET_TEST_EDITOR_UI
#include "inspector/inspector.h"
#include "inspector/property_editor_registry.h"
#include "scene/selection.h"
#include "animation/animation_system.h"
#include "asset/asset_manager.h"
#include "asset/default_assets.h"
#include "asset/registry.h"
#include "asset/serialization/animation_controller_serializer.h"
#include "render/resource/mesh.h"
#include "scene/scene_serializer.h"
#include "support/engine_fixture.h"
#include "support/imgui_context.h"
#include "support/temporary_directory.h"

#include <imgui_internal.h>

namespace CometEditor::Tests {
    class AnimationPreviewUiTest: public Comet::Tests::EngineTest {
    protected:
        Comet::Tests::ImGuiTestContext imgui{{800, 900}};
        Comet::Tests::TemporaryDirectory directory;
        Comet::AssetDatabase database{Comet::ProjectPaths{directory.path()}};
        Comet::Scene scene;
        Comet::Scene replacement;
        Comet::Entity entity = scene.create_entity("Preview");
        SelectionService selection{scene};
        EditorState state;
        Comet::SceneRuntime runtime;
        Comet::ComponentRegistry components;
        PropertyEditorRegistry widgets;
        CommandHistory history;
        PropertyEditTransaction edit{history, components};
        std::unique_ptr<InspectorPanel> inspector;

        void SetUp() override {
            EngineTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            Comet::AssetManager assets(database, engine->get_asset_registry(),
                engine->get_render_resource_factory(), engine->get_task_scheduler());
            const auto loaded =
                assets.ensure_loaded(Comet::DEFAULT_CHARACTER_MESH, Comet::AssetType::Mesh);
            ASSERT_TRUE(loaded) << loaded.error();
            entity.add_component<Comet::MeshRendererComponent>(
                Comet::DEFAULT_CHARACTER_MESH, Comet::DEFAULT_PBR_MATERIAL);
            entity.add_component<Comet::AnimatorComponent>();
            const auto builtins = Comet::create_scene_component_registry();
            ASSERT_TRUE(components.register_component(*builtins.find_component("animator")));
            widgets = create_property_editor_registry(database);
            history.bind_scene(&scene);
            selection.select_entity(entity.get_id());
            inspector = std::make_unique<InspectorPanel>(state, selection, history, edit,
                components, widgets, database, engine->get_asset_registry(),
                engine->get_material_parameters(), &runtime);
            frame();
            frame();
        }

        void TearDown() override {
            inspector.reset();
            EXPECT_TRUE(runtime.stop());
            EngineTest::TearDown();
        }

        void frame(double delta = 0) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({20, 20});
            ImGui::SetNextWindowSize({700, 840});
            inspector->render();
            ImGui::Render();
            if(const auto request = inspector->asset_inspector().take_asset_read()) {
                const auto* record = database.find(request->handle);
                ASSERT_NE(record, nullptr);
                inspector->asset_inspector().complete_asset_read(
                    *request, Comet::AnimationControllerSerializer{}.load(
                                  database.paths().assets() / record->path));
            }
            inspector->update_animation_preview(delta);
        }

        ImVec2 widget_point(const char* label) {
            auto* window = ImGui::FindWindowByName("Inspector");
            if(!window) {
                ADD_FAILURE() << "Inspector is missing";
                return {};
            }
            const auto component = ImHashStr("animator", 0, window->ID);
            const auto id = ImHashStr(label, 0, component);
            for(float y = window->WorkRect.Min.y; y < window->WorkRect.Max.y; y += 3) {
                const ImVec2 point{window->WorkRect.Min.x + 12, y};
                ImGui::GetIO().AddMousePosEvent(point.x, point.y);
                frame();
                if(ImGui::GetCurrentContext()->HoveredId == id)
                    return point;
            }
            ADD_FAILURE() << "Preview widget is missing: " << label;
            return {};
        }

        void click(const char* label, float horizontal_offset = 0) {
            auto point = widget_point(label);
            point.x += horizontal_offset;
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        }
    };

    TEST_F(AnimationPreviewUiTest,
        ControllerPreviewUsesTheExistingSceneAndYieldsToPlayAndSceneChanges) {
        Comet::AnimationController definition;
        definition.parameters = {{"speed", 0.f}};
        definition.states.front().parameter = "speed";
        definition.states.front().motions = {{0, "idle"}, {4, "walk"}};
        const auto path = database.paths().assets() / "character.ani";
        ASSERT_TRUE(Comet::AnimationControllerSerializer{}.save(definition, path));
        ASSERT_TRUE(database.scan().succeeded());
        const auto handle = database.find("character.ani")->handle;
        entity.edit_component<Comet::AnimatorComponent>(
            [&](auto& animator) { animator.controller = handle; });
        const auto registry = Comet::create_scene_component_registry();
        const auto content = Comet::SceneSerializer(registry).serialize(scene);
        ASSERT_TRUE(content);
        selection.select_asset(handle);
        frame();
        frame();
        frame(.1);
        ASSERT_TRUE(scene.get_animation_playback(entity));
        EXPECT_EQ(Comet::SceneSerializer(registry).serialize(scene).value(), content.value());
        inspector->asset_inspector().controller_editor().close();
        frame();
        EXPECT_FALSE(scene.get_animation_playback(entity));
        inspector->asset_inspector().controller_editor().open();
        frame();
        ASSERT_TRUE(scene.get_animation_playback(entity));
        auto controller = std::make_shared<const Comet::AnimationController>(definition);
        const auto animation = engine->get_asset_registry()
                                   .resolve<Comet::Mesh>(Comet::DEFAULT_CHARACTER_MESH)
                                   ->animation_data();
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::AnimationSystem>(
            [animation](Comet::AssetHandle) { return animation; },
            [controller](Comet::AssetHandle) { return controller; })));
        ASSERT_TRUE(runtime.start(scene));
        state.mode = EditorMode::Play;
        const auto running = scene.get_animation_playback(entity);
        ASSERT_TRUE(running);
        frame(.2);
        EXPECT_EQ(scene.get_animation_playback(entity), running);
        EXPECT_EQ(running->state, "idle");
        ASSERT_TRUE(runtime.stop());
        state.mode = EditorMode::Edit;
        frame();
        inspector->reset_for_scene_change();
        selection.set_scene(replacement);
        frame();
        EXPECT_FALSE(scene.get_animation_playback(entity));
        EXPECT_EQ(Comet::SceneSerializer(registry).serialize(scene).value(), content.value());
    }

    TEST_F(AnimationPreviewUiTest, ControlsPublishPreviewWithoutChangingContentOrUndo) {
        const auto builtins = Comet::create_scene_component_registry();
        const Comet::SceneSerializer serializer(builtins);
        const auto content = serializer.serialize(scene);
        ASSERT_TRUE(content);
        const auto history_state = history.state_id();
        EXPECT_FALSE(scene.get_animation_playback(entity));
        click("###Play Animation Preview");
        frame(0.1);
        auto playing = scene.get_animation_playback(entity);
        ASSERT_TRUE(playing);
        EXPECT_NEAR(playing->current.time, 0.1, 0.0001);
        click("###Animation Preview Time", 180);
        const auto positioned = scene.get_animation_playback(entity);
        ASSERT_TRUE(positioned);
        EXPECT_GT(positioned->current.time, playing->current.time);
        frame(0.5);
        EXPECT_EQ(scene.get_animation_playback(entity), positioned);
        EXPECT_NEAR(playing->current.time, 0.1, 0.0001);
        click("###Play Animation Preview");
        frame(0.1);
        playing = scene.get_animation_playback(entity);
        click("###Pause Animation Preview");
        frame(0.5);
        EXPECT_EQ(scene.get_animation_playback(entity), playing);
        EXPECT_EQ(serializer.serialize(scene).value(), content.value());
        EXPECT_EQ(history.state_id(), history_state);
        EXPECT_FALSE(history.can_undo());
        inspector->set_visible(false);
        frame();
        EXPECT_FALSE(scene.get_animation_playback(entity));
        inspector->set_visible(true);
        frame();
        click("###Play Animation Preview");
        ASSERT_TRUE(scene.get_animation_playback(entity));
        selection.clear();
        frame();
        EXPECT_FALSE(scene.get_animation_playback(entity));
    }

    TEST_F(AnimationPreviewUiTest, PlayAndSceneReplacementDoNotKeepOrOverwritePreviewState) {
        click("###Play Animation Preview");
        frame(0.1);
        const auto preview = scene.get_animation_playback(entity);
        ASSERT_TRUE(preview);
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::AnimationSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        const auto game = scene.get_animation_playback(entity);
        ASSERT_TRUE(game);
        EXPECT_NE(game, preview);
        state.mode = EditorMode::Play;
        frame(0.2);
        EXPECT_EQ(scene.get_animation_playback(entity), game);
        ASSERT_TRUE(runtime.stop());
        state.mode = EditorMode::Edit;
        frame();
        click("###Play Animation Preview");
        ASSERT_TRUE(scene.get_animation_playback(entity));
        auto next = replacement.create_entity_with_uuid(entity.get_uuid(), "Replacement");
        next.add_component<Comet::MeshRendererComponent>(
            Comet::DEFAULT_CHARACTER_MESH, Comet::DEFAULT_PBR_MATERIAL);
        next.add_component<Comet::AnimatorComponent>();
        inspector->reset_for_scene_change();
        selection.set_scene(replacement);
        selection.select_entity(next.get_id());
        history.bind_scene(&replacement);
        frame();
        EXPECT_FALSE(scene.get_animation_playback(entity));
        EXPECT_FALSE(replacement.get_animation_playback(next));
        EXPECT_DOUBLE_EQ(preview->current.time, 0.1);
    }

    TEST_F(AnimationPreviewUiTest, MissingMeshEndsPreviewAndReloadStartsWithFreshTime) {
        click("###Play Animation Preview");
        frame(0.1);
        const auto frozen = scene.get_animation_playback(entity);
        ASSERT_TRUE(frozen);
        auto& assets = engine->get_asset_registry();
        const auto mesh = assets.resolve<Comet::Mesh>(Comet::DEFAULT_CHARACTER_MESH);
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(assets.unregister_asset(Comet::DEFAULT_CHARACTER_MESH));
        frame();
        EXPECT_FALSE(scene.get_animation_playback(entity));
        ASSERT_TRUE(assets.register_asset(Comet::DEFAULT_CHARACTER_MESH, mesh));
        frame();
        EXPECT_FALSE(scene.get_animation_playback(entity));
        click("###Play Animation Preview");
        ASSERT_TRUE(scene.get_animation_playback(entity));
        EXPECT_DOUBLE_EQ(scene.get_animation_playback(entity)->current.time, 0);
        EXPECT_DOUBLE_EQ(frozen->current.time, 0.1);
    }
}
#endif
