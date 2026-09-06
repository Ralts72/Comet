#include "editor_scene_session.h"

#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "runtime/scene_runtime.h"

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
        Comet::SceneRuntime runtime;
        Comet::Scene* original_edit_scene = active_scene.get();
        const Comet::Entity edit_entity = active_scene->create_entity("Edit Entity");
        const Comet::EntityUuid entity_uuid = edit_entity.get_uuid();

        EditorSceneSession session(
            state, runtime, serializer, [&active_scene]() { return active_scene.get(); },
            [&active_scene](std::unique_ptr<Comet::Scene> replacement) {
                active_scene.swap(replacement);
                return replacement;
            });

        session.request_mode(EditorMode::Play);
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene.get(), original_edit_scene);

        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Play);
        EXPECT_TRUE(runtime.is_active());
        ASSERT_NE(active_scene.get(), original_edit_scene);
        Comet::Entity runtime_entity = active_scene->find_entity(entity_uuid);
        ASSERT_TRUE(runtime_entity);
        runtime_entity.get_component<Comet::NameComponent>().name = "Runtime Entity";

        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(active_scene.get(), original_edit_scene);
        EXPECT_EQ(active_scene->find_entity(entity_uuid)
                      .get_component<Comet::NameComponent>()
                      .name,
            "Edit Entity");
    }

    TEST(EditorSceneSessionTest, RejectsPlayWithoutAnActiveScene) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        std::unique_ptr<Comet::Scene> active_scene;
        Comet::SceneRuntime runtime;
        EditorSceneSession session(
            state, runtime, serializer, [&active_scene]() { return active_scene.get(); },
            [&active_scene](std::unique_ptr<Comet::Scene> replacement) {
                active_scene.swap(replacement);
                return replacement;
            });

        session.request_mode(EditorMode::Play);

        EXPECT_FALSE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene, nullptr);
    }

    TEST(EditorSceneSessionTest, PausedPlayStepsOnlyCloneAndStopRestoresUntouchedEdit) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        auto active = std::make_unique<Comet::Scene>();
        auto* original = active.get();
        const auto uuid = active->create_entity().get_uuid();
        Comet::SceneRuntime runtime;
        struct Motion final: Comet::System {
            Comet::EntityUuid uuid;
            explicit Motion(Comet::EntityUuid uuid) : uuid(uuid) {}
            void fixed_update(Comet::Scene& scene, const Context& context) override {
                scene.find_entity(uuid)
                    .get_component<Comet::TransformComponent>()
                    .translation.x += float(context.delta_time);
            }
        };
        runtime.add_system(std::make_unique<Motion>(uuid));
        EditorSceneSession session(
            state, runtime, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                active.swap(replacement);
                return replacement;
            });
        session.request_mode(EditorMode::Play);
        ASSERT_TRUE(session.apply_mode_request());
        runtime.set_state(Comet::SceneRuntime::State::Paused);
        runtime.advance(1, {});
        EXPECT_EQ(state.mode, EditorMode::Play);
        EXPECT_EQ(active->find_entity(uuid)
                      .get_component<Comet::TransformComponent>()
                      .translation.x,
            0);
        runtime.request_step();
        runtime.advance(1, {});
        EXPECT_NEAR(active->find_entity(uuid)
                        .get_component<Comet::TransformComponent>()
                        .translation.x,
            1.0f / 60, 1e-6f);
        EXPECT_EQ(original->find_entity(uuid)
                      .get_component<Comet::TransformComponent>()
                      .translation.x,
            0);
        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(active.get(), original);
        EXPECT_FALSE(runtime.is_active());
        session.request_mode(EditorMode::Play);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(runtime.get_state(), Comet::SceneRuntime::State::Running);
        EXPECT_EQ(runtime.get_timing().fixed_index, 0U);
    }

    TEST(EditorSceneSessionTest, FailedSystemStartRetainsEditSceneForStopRecovery) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        auto active = std::make_unique<Comet::Scene>();
        auto* original = active.get();
        Comet::SceneRuntime runtime;
        struct Failure final: Comet::System {
            void on_start(Comet::Scene&) override { throw std::runtime_error("failure"); }
        };
        runtime.add_system(std::make_unique<Failure>());
        EditorSceneSession session(
            state, runtime, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement) {
                active.swap(replacement);
                return replacement;
            });
        session.request_mode(EditorMode::Play);
        EXPECT_THROW(static_cast<void>(session.apply_mode_request()), std::runtime_error);
        EXPECT_EQ(state.mode, EditorMode::Play);
        EXPECT_NE(active.get(), original);
        EXPECT_FALSE(runtime.is_active());
        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(state.mode, EditorMode::Edit);
    }
}
