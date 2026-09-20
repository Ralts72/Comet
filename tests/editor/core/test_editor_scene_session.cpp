#include "scene/editor_scene_session.h"
#include "scene/command_history.h"

#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "scene/systems/camera_controller.h"
#include "scene/scene_runtime.h"

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
        const Comet::PostProcessSettings post_process{
            .bloom_enabled = true, .bloom_strength = 0.5f};
        ASSERT_TRUE(active_scene->set_post_process(post_process));
        Comet::Scene* original_edit_scene = active_scene.get();
        Comet::Entity edit_entity = active_scene->create_entity("Edit Entity");
        edit_entity.add_component<Comet::CameraComponent>().primary = true;
        edit_entity.add_component<Comet::CameraControllerComponent>().move_speed = 5;
        const Comet::EntityUuid entity_uuid = edit_entity.get_uuid();
        CommandHistory history;
        history.bind_scene(active_scene.get());
        const auto saved_state = history.state_id();
        PropertyEditTransaction edit(history, component_registry());
        ASSERT_TRUE(edit.apply({entity_uuid, "name", "name"}, std::string("Edited Name")));
        const auto edited_state = history.state_id();
        std::vector<EditorMode> installed_modes;
        Comet::SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<Comet::CameraControllerSystem>()));

        EditorSceneSession session(
            state, serializer, [&active_scene]() { return active_scene.get(); },
            [&active_scene, &installed_modes, &history, &runtime](
                std::unique_ptr<Comet::Scene> replacement, EditorMode mode) {
                EXPECT_TRUE(runtime.stop());
                installed_modes.push_back(mode);
                active_scene.swap(replacement);
                if(mode == EditorMode::Edit && history.get_scene() != active_scene.get())
                    history.bind_scene(active_scene.get());
                return replacement;
            },
            [&] { return runtime.start(*active_scene); });

        session.request_mode(EditorMode::Play);
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene.get(), original_edit_scene);

        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Play);
        EXPECT_TRUE(runtime.is_active());
        ASSERT_NE(active_scene.get(), original_edit_scene);
        EXPECT_EQ(active_scene->get_post_process(), post_process);
        ASSERT_TRUE(active_scene->set_post_process({}));
        Comet::Entity runtime_entity = active_scene->find_entity(entity_uuid);
        ASSERT_TRUE(runtime_entity);
        runtime_entity.get_component<Comet::NameComponent>().name = "Runtime Entity";
        Comet::Input input;
        input.focus_event(true);
        input.key_event(Comet::Input::Key::W, true);
        ASSERT_TRUE(runtime.advance(0.1, &input.publish_frame()));
        EXPECT_FLOAT_EQ(
            runtime_entity.get_component<Comet::TransformComponent>().translation.z, -0.5f);
        EXPECT_EQ(edit_entity.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(0));

        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_FALSE(runtime.is_active());
        ASSERT_TRUE(runtime.advance(0.1, &input.publish_frame()));
        EXPECT_EQ(active_scene.get(), original_edit_scene);
        EXPECT_EQ(active_scene->get_post_process(), post_process);
        EXPECT_EQ(edit_entity.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(0));
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
        Comet::SceneRuntime runtime;
        EditorSceneSession session(
            state, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> candidate, EditorMode) {
                EXPECT_TRUE(runtime.stop());
                ++installations;
                active.swap(candidate);
                return candidate;
            },
            [&] { return runtime.start(*active); },
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
        Comet::SceneRuntime runtime;
        EditorSceneSession session(
            state, serializer, [&active_scene]() { return active_scene.get(); },
            [&active_scene, &runtime](std::unique_ptr<Comet::Scene> replacement, EditorMode) {
                EXPECT_TRUE(runtime.stop());
                active_scene.swap(replacement);
                return replacement;
            },
            [&] { return runtime.start(*active_scene); });

        session.request_mode(EditorMode::Play);

        EXPECT_FALSE(session.apply_mode_request());
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active_scene, nullptr);
    }

    TEST(EditorSceneSessionTest, FailedSystemStartRestoresEditSceneAndAllowsExplicitRetry) {
        class StartupSystem final: public Comet::System {
        public:
            bool fail = true;
            int stopped = 0;
            Comet::Result<void, Comet::Error> on_start(Comet::Scene& scene) override {
                scene.create_entity("Runtime only");
                if(fail)
                    return Comet::Result<void, Comet::Error>::failure({"Startup failed"});
                return Comet::Result<void, Comet::Error>::success();
            }
            void on_stop(Comet::Scene& scene) noexcept override {
                EXPECT_EQ(scene.get_entities().size(), 2u);
                ++stopped;
            }
        };
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        auto active = std::make_unique<Comet::Scene>();
        active->create_entity("Original");
        const auto original = active.get();
        Comet::SceneRuntime runtime;
        auto system = std::make_unique<StartupSystem>();
        auto* probe = system.get();
        ASSERT_TRUE(runtime.add_system(std::move(system)));
        EditorSceneSession session(
            state, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> candidate, EditorMode) {
                EXPECT_TRUE(runtime.stop());
                active.swap(candidate);
                return candidate;
            },
            [&] { return runtime.start(*active); });

        session.request_mode(EditorMode::Play);
        const auto failed = session.apply_mode_request();
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().message, "Startup failed");
        EXPECT_EQ(state.mode, EditorMode::Edit);
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(active->get_entities().size(), 1u);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(probe->stopped, 1);
        probe->fail = false;
        session.request_mode(EditorMode::Play);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_TRUE(runtime.is_active());
        EXPECT_EQ(state.mode, EditorMode::Play);
        session.request_mode(EditorMode::Edit);
        ASSERT_TRUE(session.apply_mode_request());
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(active.get(), original);
        EXPECT_EQ(probe->stopped, 2);
    }

    TEST(EditorSceneSessionTest, FailedCloneKeepsEditSceneAndAllowsExplicitRetry) {
        const Comet::SceneSerializer serializer(component_registry());
        EditorState state;
        auto active = std::make_unique<Comet::Scene>();
        const auto original = active.get();
        auto entity = active->create_entity(std::string(1, '\xff'));
        int replacements = 0;
        Comet::SceneRuntime runtime;
        EditorSceneSession session(
            state, serializer, [&] { return active.get(); },
            [&](std::unique_ptr<Comet::Scene> replacement, EditorMode) {
                EXPECT_TRUE(runtime.stop());
                ++replacements;
                active.swap(replacement);
                return replacement;
            },
            [&] { return runtime.start(*active); });

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
