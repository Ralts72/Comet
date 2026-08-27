#include "editor_scene_session.h"

#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

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

        EditorSceneSession session(
            state,
            serializer,
            [&active_scene]() { return active_scene.get(); },
            [&active_scene](std::unique_ptr<Comet::Scene> replacement) {
                active_scene.swap(replacement);
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
        EXPECT_EQ(
            active_scene->find_entity(entity_uuid)
                .get_component<Comet::NameComponent>().name,
            "Edit Entity");
    }

    TEST(EditorSceneSessionTest, RejectsPlayWithoutAnActiveScene) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        std::unique_ptr<Comet::Scene> active_scene;
        EditorSceneSession session(
            state,
            serializer,
            [&active_scene]() { return active_scene.get(); },
            [&active_scene](std::unique_ptr<Comet::Scene> replacement) {
                active_scene.swap(replacement);
                return replacement;
            });

        session.request_mode(EditorMode::Play);

        EXPECT_FALSE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene, nullptr);
    }
}
