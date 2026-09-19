#include "scene/editor_scene_session.h"
#include "scene/command_history.h"

#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace CometEditor::Tests {
    namespace {
        const Comet::ComponentRegistry& component_registry() {
            static const Comet::ComponentRegistry registry =
                Comet::create_scene_component_registry();
            return registry;
        }
    }

    TEST(EditorSceneSessionTest, DefersPlayAndRestoresOriginalEditScene) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        auto active_scene = std::make_unique<Comet::Scene>();
        Comet::Scene* original_edit_scene = active_scene.get();
        const Comet::Entity edit_entity = active_scene->create_entity("Edit Entity");
        const Comet::EntityUuid entity_uuid = edit_entity.get_uuid();
        CommandHistory history;
        history.bind_scene(active_scene.get());
        const auto saved_state = history.state_id();
        PropertyEditTransaction edit(history, component_registry());
        ASSERT_TRUE(edit.apply({entity_uuid, "name", "name"}, std::string("Edited Name")));
        const auto edited_state = history.state_id();
        std::vector<EditorMode> installed_modes;

        EditorSceneSession session(
            state, serializer, [&active_scene]() { return active_scene.get(); },
            [&active_scene, &installed_modes, &history](
                std::unique_ptr<Comet::Scene> replacement, EditorMode mode) {
                installed_modes.push_back(mode);
                active_scene.swap(replacement);
                if(mode == EditorMode::Edit && history.get_scene() != active_scene.get())
                    history.bind_scene(active_scene.get());
                return replacement;
            });

        session.request_mode(EditorMode::Play);
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene.get(), original_edit_scene);

        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Play);
        ASSERT_NE(active_scene.get(), original_edit_scene);
        Comet::Entity runtime_entity = active_scene->find_entity(entity_uuid);
        ASSERT_TRUE(runtime_entity);
        runtime_entity.get_component<Comet::NameComponent>().name = "Runtime Entity";

        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene.get(), original_edit_scene);
        EXPECT_EQ(active_scene->find_entity(entity_uuid).get_component<Comet::NameComponent>().name,
            "Edited Name");
        EXPECT_EQ(history.state_id(), edited_state);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(history.state_id(), saved_state);
        EXPECT_EQ(active_scene->find_entity(entity_uuid).get_component<Comet::NameComponent>().name,
            "Edit Entity");
        EXPECT_EQ(installed_modes, (std::vector{EditorMode::Play, EditorMode::Edit}));
    }

    TEST(EditorSceneSessionTest, PreparationFailureAllowsRetryAndStopSkipsPreparation) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        auto active = std::make_unique<Comet::Scene>();
        const auto edit = active.get();
        bool accept = false;
        const Comet::Error preparation_error{"rejected", std::make_error_code(std::errc::io_error)};
        int preparations = 0;
        int installations = 0;
        EditorSceneSession session(
            state, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> candidate, EditorMode) {
                ++installations;
                active.swap(candidate);
                return candidate;
            },
            [&](Comet::Scene& candidate) {
                ++preparations;
                EXPECT_NE(&candidate, edit);
                EXPECT_EQ(active.get(), edit);
                EXPECT_EQ(state.mode, EditorMode::Edit);
                if(!accept)
                    return Comet::Result<void, Comet::Error>::failure(preparation_error);
                return Comet::Result<void, Comet::Error>::success();
            });
        session.request_mode(EditorMode::Play);
        const auto rejected = session.apply_mode_request();
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().message, preparation_error.message);
        EXPECT_EQ(rejected.error().code, preparation_error.code);
        EXPECT_EQ(installations, 0);
        EXPECT_EQ(active.get(), edit);
        EXPECT_EQ(state.mode, EditorMode::Edit);
        accept = true;
        session.request_mode(EditorMode::Play);
        ASSERT_TRUE(session.apply_mode_request());
        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(preparations, 2);
        EXPECT_EQ(installations, 2);
        EXPECT_EQ(active.get(), edit);
        EXPECT_EQ(state.mode, EditorMode::Edit);
    }

    TEST(EditorSceneSessionTest, RejectsPlayWithoutAnActiveScene) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        std::unique_ptr<Comet::Scene> active_scene;
        EditorSceneSession session(
            state, serializer, [&active_scene]() { return active_scene.get(); },
            [&active_scene](std::unique_ptr<Comet::Scene> replacement, EditorMode) {
                active_scene.swap(replacement);
                return replacement;
            });

        session.request_mode(EditorMode::Play);

        EXPECT_FALSE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene, nullptr);
    }

    TEST(EditorSceneSessionTest, FailedCloneKeepsEditSceneAndAllowsExplicitRetry) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        auto active = std::make_unique<Comet::Scene>();
        const auto original = active.get();
        auto entity = active->create_entity(std::string(1, '\xff'));
        int replacements = 0;
        EditorSceneSession session(
            state, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement, EditorMode) {
                ++replacements;
                active.swap(replacement);
                return replacement;
            });

        session.request_mode(EditorMode::Play);
        EXPECT_FALSE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(replacements, 0);

        entity.get_component<Comet::NameComponent>().name = "Repaired";
        const auto idle = session.apply_mode_request();
        ASSERT_TRUE(idle);
        EXPECT_FALSE(idle.value());
        session.request_mode(EditorMode::Play);
        EXPECT_TRUE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Play);
        EXPECT_EQ(replacements, 1);
    }
}
