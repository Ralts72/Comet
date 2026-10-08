#include "scene/scene_runtime.h"
#include "scene/systems/camera_controller.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "input/player_input_settings.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Comet::Tests {
    namespace {
        using UpdateResult = Result<void, Error>;
        struct Sample {
            double delta;
            double time;
            uint64_t index;
            Input::Frame input;
        };
        struct Calls {
            std::vector<std::string> order;
            std::vector<Sample> fixed;
            std::vector<Sample> updates;
        };
        class RecordingSystem final: public System {
        public:
            RecordingSystem(Calls& calls, std::string name) : calls(calls), name(std::move(name)) {}
            UpdateResult on_start(
                Scene& scene, RuntimeSession& session, const RuntimeServices&) override {
                EXPECT_TRUE(session.is_bound_to(scene));
                calls.order.push_back("start " + name);
                if(start)
                    return start(scene);
                return UpdateResult::success();
            }
            UpdateResult fixed_update(Scene& scene, const Context& context) override {
                calls.order.push_back("fixed " + name);
                calls.fixed.push_back({context.delta_time, context.total_time, context.index,
                    context.input.physical()});
                if(fixed)
                    return fixed(scene, context);
                return UpdateResult::success();
            }
            UpdateResult update(Scene& scene, const Context& context) override {
                calls.order.push_back("update " + name);
                calls.updates.push_back({context.delta_time, context.total_time, context.index,
                    context.input.physical()});
                if(update_frame)
                    return update_frame(scene, context);
                return UpdateResult::success();
            }
            void on_pause_changed(bool paused) noexcept override {
                if(pause)
                    pause(paused);
            }
            void on_stop(
                Scene& scene, RuntimeSession& session, const RuntimeServices&) noexcept override {
                EXPECT_TRUE(session.is_bound_to(scene));
                calls.order.push_back("stop " + name);
                if(stop)
                    stop(scene);
            }
            bool wants_cursor_capture(Scene& scene, const InputState& input) const override {
                return capture && capture(scene, input);
            }
            std::function<UpdateResult(Scene&)> start;
            std::function<UpdateResult(Scene&, const Context&)> fixed;
            std::function<UpdateResult(Scene&, const Context&)> update_frame;
            std::function<void(bool)> pause;
            std::function<void(Scene&)> stop;
            std::function<bool(Scene&, const InputState&)> capture;

        private:
            Calls& calls;
            std::string name;
        };
    }

    class SceneRuntimeTest: public testing::Test {
    protected:
        using State = SceneRuntime::State;
        Scene scene;
        Calls calls;
        SceneRuntime runtime;
        Input input;

        void SetUp() override {
            input.focus_event(true);
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1}));
        }
        RecordingSystem* add(std::string name = "A") {
            auto system = std::make_unique<RecordingSystem>(calls, std::move(name));
            auto* observer = system.get();
            EXPECT_TRUE(runtime.add_system(std::move(system)));
            return observer;
        }
        void advance(double delta) {
            const auto& frame = input.publish_frame();
            ASSERT_TRUE(runtime.advance(delta, &frame));
        }
    };

    TEST_F(SceneRuntimeTest, SystemQueryFollowsRegistrationAndClearWithoutOwningSystems) {
        EXPECT_EQ(runtime.find_system<RecordingSystem>(), nullptr);
        EXPECT_EQ(runtime.find_system<CameraControllerSystem>(), nullptr);
        auto* first = add("First");
        add("Second");
        ASSERT_TRUE(runtime.add_system(std::make_unique<CameraControllerSystem>()));
        const auto& observer = runtime;
        EXPECT_EQ(observer.find_system<RecordingSystem>(), first);
        EXPECT_NE(observer.find_system<CameraControllerSystem>(), nullptr);
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(observer.find_system<RecordingSystem>(), first);
        EXPECT_FALSE(runtime.clear_systems());
        EXPECT_EQ(observer.find_system<RecordingSystem>(), first);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(observer.find_system<RecordingSystem>(), first);
        ASSERT_TRUE(runtime.clear_systems());
        EXPECT_EQ(observer.find_system<RecordingSystem>(), nullptr);
        EXPECT_EQ(observer.find_system<CameraControllerSystem>(), nullptr);
        auto* replacement = add("Replacement");
        EXPECT_EQ(observer.find_system<RecordingSystem>(), replacement);
    }

    TEST_F(SceneRuntimeTest, CursorCaptureRequiresFreshAuthorizationAcrossRuntimeBoundaries) {
        add("No capture");
        auto* system = add("Capture");
        system->capture = [](Scene&, const InputState&) { return true; };
        system->start = [&](Scene&) {
            EXPECT_FALSE(runtime.wants_cursor_capture());
            return UpdateResult::success();
        };
        system->fixed = system->update_frame = [&](Scene&, const System::Context&) {
            EXPECT_FALSE(runtime.wants_cursor_capture());
            return UpdateResult::success();
        };
        system->stop = [&](Scene&) { EXPECT_FALSE(runtime.wants_cursor_capture()); };
        EXPECT_FALSE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        advance(0.1);
        EXPECT_TRUE(runtime.wants_cursor_capture());

        auto pointer_blocked = input.publish_frame();
        pointer_blocked.pointer_enabled = false;
        ASSERT_TRUE(runtime.advance(0, &pointer_blocked));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        advance(0);
        EXPECT_TRUE(runtime.wants_cursor_capture());
        input.focus_event(false);
        advance(0);
        EXPECT_FALSE(runtime.wants_cursor_capture());
        input.focus_event(true);
        advance(0);
        EXPECT_TRUE(runtime.wants_cursor_capture());

        ASSERT_TRUE(runtime.set_state(State::Paused));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        advance(1);
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_FALSE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.set_state(State::Running));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        advance(0);
        EXPECT_TRUE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.discard_input());
        EXPECT_FALSE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        advance(0);
        EXPECT_TRUE(runtime.wants_cursor_capture());

        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.wants_cursor_capture());
        advance(0);
        EXPECT_TRUE(runtime.wants_cursor_capture());
        system->update_frame = [](Scene&, const System::Context&) {
            return UpdateResult::failure({"Capture consumer failed"});
        };
        EXPECT_FALSE(runtime.advance(0, &input.publish_frame()));
        EXPECT_FALSE(runtime.wants_cursor_capture());
    }

    TEST_F(SceneRuntimeTest, CameraCaptureUsesRoutedActionsAndTheCommittedScene) {
        auto actions = InputActions::create(
            {{"camera.look", InputActions::Type::Button, {{Input::Key::K}}, "camera"},
                {"menu.look", InputActions::Type::Button, {{Input::Key::K}}, "menu"}},
            {{"camera", true}, {"menu", false, 10, true}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(actions.value()));
        auto camera = scene.create_entity("Camera");
        camera.add_component<CameraComponent>().primary = true;
        camera.add_component<CameraControllerComponent>();
        ASSERT_TRUE(runtime.add_system(std::make_unique<CameraControllerSystem>()));
        auto* destroyer = add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::K, true);
        advance(0);
        EXPECT_TRUE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.get_session().request_input_context("menu", true));
        advance(0);
        EXPECT_FALSE(runtime.wants_cursor_capture());
        ASSERT_TRUE(runtime.get_session().request_input_context("menu", false));
        advance(0);
        EXPECT_TRUE(runtime.wants_cursor_capture());
        destroyer->update_frame = [&](Scene& current, const System::Context&) {
            EXPECT_TRUE(current.request_destroy_entity(camera));
            return UpdateResult::success();
        };
        advance(0);
        EXPECT_FALSE(camera);
        EXPECT_FALSE(runtime.wants_cursor_capture());
    }

    TEST_F(SceneRuntimeTest, ActiveRebindingKeepsWorldTimingAndRejectsEveryRuntimeCallback) {
        const auto action_id = Uuid::generate();
        const auto binding_id = Uuid::generate();
        const auto actions = InputActions::create({{"jump", InputActions::Type::Button,
            {{Input::Key::Space, 1, 0, binding_id}}, {}, action_id}});
        const auto replacement = InputActions::create({{"jump", InputActions::Type::Button,
            {{Input::Key::J, 1, 0, binding_id}}, {}, action_id}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.set_input_actions(actions.value()));
        EXPECT_FALSE(runtime.rebind_input_actions(replacement.value()));
        auto entity = scene.create_entity("Kept world");
        const auto entity_id = entity.get_uuid();
        int starts = 0;
        int stops = 0;
        int pauses = 0;
        std::vector<InputState::Action> fixed;
        std::vector<InputState::Action> updates;
        auto* system = add();
        system->start = [&](Scene&) {
            ++starts;
            EXPECT_FALSE(runtime.rebind_input_actions(replacement.value()));
            return UpdateResult::success();
        };
        system->fixed = [&](Scene&, const System::Context& context) {
            EXPECT_FALSE(runtime.rebind_input_actions(actions.value()));
            fixed.push_back(*context.input.action("jump"));
            return UpdateResult::success();
        };
        system->update_frame = [&](Scene&, const System::Context& context) {
            EXPECT_FALSE(runtime.rebind_input_actions(actions.value()));
            updates.push_back(*context.input.action("jump"));
            return UpdateResult::success();
        };
        system->pause = [&](bool) {
            ++pauses;
            EXPECT_FALSE(runtime.rebind_input_actions(actions.value()));
        };
        system->stop = [&](Scene&) {
            ++stops;
            EXPECT_FALSE(runtime.rebind_input_actions(replacement.value()));
        };
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.set_input_actions(replacement.value()));
        input.key_event(Input::Key::Space, true);
        advance(0.05);
        ASSERT_TRUE(fixed.empty());
        ASSERT_EQ(updates.size(), 1u);
        EXPECT_TRUE(updates.back().pressed);
        const auto before = runtime.get_timing();
        ASSERT_TRUE(runtime.rebind_input_actions(replacement.value()));
        EXPECT_EQ(runtime.get_timing().frame_index, before.frame_index);
        EXPECT_EQ(runtime.get_timing().fixed_index, before.fixed_index);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, before.total_time);
        EXPECT_DOUBLE_EQ(runtime.get_timing().interpolation, before.interpolation);
        input.key_event(Input::Key::J, true);
        advance(0.05);
        ASSERT_EQ(fixed.size(), 1u);
        EXPECT_EQ(runtime.get_timing().fixed_index, 1u);
        EXPECT_TRUE(fixed.back().down);
        EXPECT_FALSE(fixed.back().pressed);
        EXPECT_FALSE(updates.back().pressed);
        EXPECT_EQ(starts, 1);
        EXPECT_EQ(stops, 0);
        EXPECT_EQ(scene.find_entity(entity_id), entity);
        EXPECT_EQ(scene.entity_count(), 1u);

        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.rebind_input_actions(actions.value()));
        advance(0.2);
        EXPECT_EQ(fixed.size(), 1u);
        EXPECT_EQ(updates.size(), 2u);
        ASSERT_TRUE(runtime.request_step());
        advance(0.2);
        ASSERT_EQ(fixed.size(), 2u);
        EXPECT_TRUE(fixed.back().down);
        EXPECT_FALSE(fixed.back().pressed);
        ASSERT_TRUE(runtime.set_state(State::Running));
        EXPECT_EQ(starts, 1);
        EXPECT_EQ(pauses, 3);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(stops, 1);
        EXPECT_FALSE(runtime.rebind_input_actions(replacement.value()));
    }

    TEST_F(SceneRuntimeTest, StopCancelsUnpreparedRebindingButRetainsAppliedMapping) {
        const auto action_id = Uuid::generate();
        const auto binding_id = Uuid::generate();
        const auto actions = InputActions::create({{"jump", InputActions::Type::Button,
            {{Input::Key::Space, 1, 0, binding_id}}, {}, action_id}});
        const auto replacement = InputActions::create({{"jump", InputActions::Type::Button,
            {{Input::Key::J, 1, 0, binding_id}}, {}, action_id}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.set_input_actions(actions.value()));
        std::vector<InputState::Action> updates;
        auto* system = add();
        system->update_frame = [&](Scene&, const System::Context& context) {
            updates.push_back(*context.input.action("jump"));
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.rebind_input_actions(replacement.value()));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::Space, true);
        advance(0);
        ASSERT_EQ(updates.size(), 1u);
        EXPECT_TRUE(updates.back().pressed);
        ASSERT_TRUE(runtime.rebind_input_actions(replacement.value()));
        advance(0);
        EXPECT_TRUE(updates.back().released);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::J, true);
        advance(0);
        EXPECT_TRUE(updates.back().pressed);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, OrdersPhasesAndStopsInReverseBeforeRestartingCleanly) {
        add("A");
        add("B");
        ASSERT_TRUE(runtime.start(scene));
        advance(0.21);
        EXPECT_EQ(calls.order, (std::vector<std::string>{"start A", "start B", "fixed A", "fixed B",
                                   "fixed A", "fixed B", "update A", "update B"}));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 2u);
        EXPECT_EQ(runtime.get_timing().fixed_index, 2u);
        EXPECT_NEAR(runtime.get_timing().fixed_time, 0.2, 1e-9);
        EXPECT_NEAR(runtime.get_timing().interpolation, 0.1, 1e-9);
        EXPECT_DOUBLE_EQ(calls.updates.front().delta, 0.21);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(calls.order[calls.order.size() - 2], "stop B");
        EXPECT_EQ(calls.order.back(), "stop A");
        const auto stopped_calls = calls.order.size();
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(calls.order.size(), stopped_calls);
        ASSERT_TRUE(runtime.start(scene));
        advance(0.09);
        EXPECT_EQ(runtime.get_timing().fixed_index, 0u);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.clear_systems());
    }

    TEST_F(SceneRuntimeTest, InitiallyPausedStartPrecedesSystemStartupAndStepsOnlyOnRequest) {
        auto* first = add("A");
        auto* second = add("B");
        first->pause = [&](bool paused) {
            EXPECT_EQ(paused, runtime.get_state() == State::Paused);
            EXPECT_FALSE(runtime.stop());
            calls.order.push_back(paused ? "pause A" : "resume A");
        };
        second->pause = [&](bool paused) {
            calls.order.push_back(paused ? "pause B" : "resume B");
        };
        EXPECT_FALSE(runtime.start(scene, static_cast<State>(99)));
        EXPECT_FALSE(
            runtime.start(scene, State::Running, static_cast<SceneRuntime::InputStart>(99)));
        EXPECT_FALSE(runtime.is_active());
        ASSERT_TRUE(runtime.start(scene, State::Paused));
        EXPECT_EQ(
            calls.order, (std::vector<std::string>{"pause A", "start A", "pause B", "start B"}));
        advance(1);
        EXPECT_TRUE(calls.fixed.empty());
        EXPECT_TRUE(calls.updates.empty());
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_EQ(calls.fixed.size(), 2u);
        EXPECT_EQ(calls.updates.size(), 2u);
        EXPECT_EQ(runtime.get_state(), State::Paused);
        advance(1);
        EXPECT_EQ(calls.fixed.size(), 2u);
        ASSERT_TRUE(runtime.stop());
        calls.order.clear();
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(
            calls.order, (std::vector<std::string>{"resume A", "start A", "resume B", "start B"}));
        EXPECT_EQ(runtime.get_state(), State::Running);
        ASSERT_TRUE(runtime.stop());
        calls.order.clear();
        second->start = [](Scene&) { return UpdateResult::failure({"startup failed"}); };
        EXPECT_FALSE(runtime.start(scene, State::Paused));
        EXPECT_EQ(calls.order, (std::vector<std::string>{"pause A", "start A", "pause B", "start B",
                                   "stop B", "stop A"}));
        EXPECT_FALSE(runtime.is_active());
    }

    TEST_F(SceneRuntimeTest, RestartRequestsCoalesceWithoutInterruptingTheCurrentPhase) {
        EXPECT_FALSE(runtime.get_session().request_restart());
        EXPECT_FALSE(runtime.take_restart_request());
        const auto entity = scene.create_entity("Unchanged until host restart");
        auto* requester = add("requester");
        requester->update_frame = [&](Scene&, const System::Context& context) {
            EXPECT_TRUE(context.session.request_restart());
            EXPECT_FALSE(runtime.take_restart_request());
            EXPECT_TRUE(context.session.request_restart());
            EXPECT_TRUE(context.session.set_value("after.request", true));
            return UpdateResult::success();
        };
        auto* observer = add("observer");
        observer->update_frame = [&](Scene& current, const System::Context& context) {
            EXPECT_TRUE(current.is_valid(entity));
            EXPECT_TRUE(context.session.get_value("after.request"));
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        advance(0);
        EXPECT_TRUE(runtime.is_active());
        EXPECT_TRUE(runtime.take_restart_request());
        EXPECT_FALSE(runtime.take_restart_request());

        ASSERT_TRUE(runtime.set_state(State::Paused));
        advance(1);
        EXPECT_FALSE(runtime.take_restart_request());
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_EQ(runtime.get_state(), State::Paused);
        EXPECT_TRUE(runtime.take_restart_request());
        EXPECT_FALSE(runtime.take_restart_request());
        EXPECT_TRUE(runtime.get_session().request_restart());
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(runtime.take_restart_request());
        EXPECT_FALSE(runtime.get_session().request_restart());
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.take_restart_request());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, EntityRequestsCommitOnlyAfterAllSystemsInEachPhase) {
        auto* requester = add("requester");
        auto* observer = add("observer");
        EXPECT_FALSE(scene.request_create_entity("Inactive"));
        EXPECT_FALSE(scene.request_destroy_entity(scene.create_entity("Inactive")));
        EntityUuid created;
        requester->start = [&](Scene& current) -> UpdateResult {
            const auto request = current.request_create_entity("Spawned");
            if(!request)
                return UpdateResult::failure({"Cannot queue startup entity"});
            created = *request;
            EXPECT_FALSE(current.find_entity(created));
            return UpdateResult::success();
        };
        observer->start = [&](Scene& current) -> UpdateResult {
            EXPECT_FALSE(current.find_entity(created));
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.find_entity(created));
        const auto& transform = scene.find_entity(created).get_component<TransformComponent>();
        EXPECT_EQ(transform.translation, Math::Vec3(0));
        EXPECT_EQ(transform.rotation, Math::Vec3(0));
        EXPECT_EQ(transform.scale, Math::Vec3(1));
        EXPECT_FALSE(scene.find_entity(created).has_component<MeshRendererComponent>());
        EXPECT_FALSE(scene.request_create_entity(std::string(129, 'x')));

        requester->fixed = [&](Scene& current, const System::Context&) -> UpdateResult {
            const auto entity = current.find_entity(created);
            EXPECT_TRUE(current.request_destroy_entity(entity));
            EXPECT_TRUE(current.request_destroy_entity(entity));
            return UpdateResult::success();
        };
        observer->fixed = [&](Scene& current, const System::Context&) -> UpdateResult {
            EXPECT_TRUE(current.find_entity(created));
            return UpdateResult::success();
        };
        advance(0.1);
        EXPECT_FALSE(scene.find_entity(created));

        requester->update_frame = [&](Scene& current, const System::Context&) -> UpdateResult {
            const auto request = current.request_create_entity("Updated");
            if(!request)
                return UpdateResult::failure({"Cannot queue update entity"});
            created = *request;
            EXPECT_FALSE(current.find_entity(created));
            return UpdateResult::success();
        };
        observer->update_frame = [&](Scene& current, const System::Context&) -> UpdateResult {
            EXPECT_FALSE(current.find_entity(created));
            return UpdateResult::success();
        };
        advance(0);
        EXPECT_TRUE(scene.find_entity(created));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.request_create_entity("Stopped"));
    }

    TEST_F(SceneRuntimeTest, EntityCreationOwnsItsInitialValuesUntilThePhaseCommits) {
        auto* observer = add();
        ASSERT_TRUE(runtime.start(scene));
        Scene::EntityCreation creation{
            .transform = {.translation = {2, 3, 4}, .rotation = {10, 20, 30}, .scale = {0, -2, 3}},
            .mesh_renderer = MeshRendererComponent{AssetHandle{11}, AssetHandle{22}}};
        const auto expected = creation;
        std::string name = "Snapshot";
        const auto requested = scene.request_create_entity(name, creation);
        ASSERT_TRUE(requested);
        name = "Changed";
        creation.transform.translation = Math::Vec3(100);
        creation.mesh_renderer->mesh = AssetHandle{33};
        creation.mesh_renderer->material = AssetHandle{44};
        observer->update_frame = [&](Scene& current, const System::Context&) {
            EXPECT_FALSE(current.find_entity(*requested));
            return UpdateResult::success();
        };
        advance(0);

        const auto created = scene.find_entity(*requested);
        ASSERT_TRUE(created);
        EXPECT_EQ(created.get_component<NameComponent>().name, "Snapshot");
        EXPECT_EQ(created.get_component<TransformComponent>().translation,
            expected.transform.translation);
        EXPECT_EQ(
            created.get_component<TransformComponent>().rotation, expected.transform.rotation);
        EXPECT_EQ(created.get_component<TransformComponent>().scale, expected.transform.scale);
        ASSERT_TRUE(created.has_component<MeshRendererComponent>());
        EXPECT_EQ(
            created.get_component<MeshRendererComponent>().mesh, expected.mesh_renderer->mesh);
        EXPECT_EQ(created.get_component<MeshRendererComponent>().material,
            expected.mesh_renderer->material);
        EXPECT_FALSE(scene.get_parent(created));
        EXPECT_TRUE(scene.get_children(created).empty());
        EXPECT_EQ(Math::Vec3(scene.get_world_matrix(created)[3]), expected.transform.translation);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, InvalidEntityCreationDoesNotConsumeQueueCapacity) {
        ASSERT_TRUE(runtime.start(scene));
        for(const auto member : {&TransformComponent::translation, &TransformComponent::rotation,
                &TransformComponent::scale}) {
            for(const float invalid :
                {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
                for(int axis = 0; axis < 3; ++axis) {
                    Scene::EntityCreation creation;
                    (creation.transform.*member)[axis] = invalid;
                    EXPECT_FALSE(scene.request_create_entity("Invalid transform", creation));
                }
            }
        }
        for(const auto renderer :
            {MeshRendererComponent{}, MeshRendererComponent{AssetHandle{11}, {}},
                MeshRendererComponent{{}, AssetHandle{22}}}) {
            EXPECT_FALSE(
                scene.request_create_entity("Invalid renderer", {.mesh_renderer = renderer}));
        }
        EXPECT_FALSE(scene.request_create_entity(std::string_view("bad\0name", 8)));
        for(int index = 0; index < 1024; ++index)
            ASSERT_TRUE(scene.request_create_entity());
        EXPECT_FALSE(scene.request_create_entity("Queue full"));
        EXPECT_EQ(scene.entity_count(), 0u);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        advance(0);
        EXPECT_EQ(scene.entity_count(), 0u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, PausedEntityCreationPublishesAfterTheSteppedFixedPhase) {
        auto* requester = add("requester");
        auto* observer = add("observer");
        EntityUuid created;
        requester->fixed = [&](Scene& current, const System::Context&) {
            const auto requested = current.request_create_entity("Stepped",
                {.transform = {.translation = {1, 2, 3}},
                    .mesh_renderer = MeshRendererComponent{AssetHandle{11}, AssetHandle{22}}});
            if(!requested)
                return UpdateResult::failure({"Cannot queue stepped entity"});
            created = *requested;
            return UpdateResult::success();
        };
        observer->fixed = [&](Scene& current, const System::Context&) {
            EXPECT_FALSE(current.find_entity(created));
            return UpdateResult::success();
        };
        observer->update_frame = [&](Scene& current, const System::Context&) {
            const auto entity = current.find_entity(created);
            EXPECT_TRUE(entity);
            if(entity) {
                EXPECT_EQ(
                    entity.get_component<TransformComponent>().translation, Math::Vec3(1, 2, 3));
                EXPECT_TRUE(entity.has_component<MeshRendererComponent>());
            }
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.set_state(State::Paused));
        advance(1);
        EXPECT_EQ(scene.entity_count(), 0u);
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_EQ(scene.entity_count(), 1u);
        advance(1);
        EXPECT_EQ(scene.entity_count(), 1u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, StopDiscardsPendingCreationWithoutPublishingStopRequests) {
        auto* requester = add();
        const Scene::EntityCreation creation{.transform = {.translation = {1, 2, 3}},
            .mesh_renderer = MeshRendererComponent{AssetHandle{11}, AssetHandle{22}}};
        requester->stop = [&](Scene& current) {
            EXPECT_TRUE(current.request_create_entity("During stop", creation));
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.request_create_entity("Pending", creation));
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(scene.entity_count(), 0u);
        ASSERT_TRUE(runtime.start(scene));
        advance(0);
        EXPECT_EQ(scene.entity_count(), 0u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, FailedPhaseDiscardsUncommittedEntityRequests) {
        auto* requester = add();
        requester->update_frame = [&](Scene& current, const System::Context&) -> UpdateResult {
            EXPECT_TRUE(current.request_create_entity("Discarded",
                {.transform = {.translation = {1, 2, 3}},
                    .mesh_renderer = MeshRendererComponent{AssetHandle{11}, AssetHandle{22}}}));
            EXPECT_TRUE(runtime.get_session().set_value("game.score", 2.0f));
            EXPECT_TRUE(runtime.get_session().request_restart());
            return UpdateResult::failure({"phase failed"});
        };
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.advance(0));
        EXPECT_EQ(scene.entity_count(), 0u);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(scene.request_create_entity("Inactive"));
        EXPECT_FALSE(runtime.get_session().get_value("game.score"));
        EXPECT_FALSE(runtime.take_restart_request());
    }

    TEST_F(SceneRuntimeTest, RigidBodyRemovalCommitsAfterEachPhaseAndPreservesOtherComponents) {
        auto startup = scene.create_entity("Startup");
        auto fixed = scene.create_entity("Fixed");
        auto updated = scene.create_entity("Update");
        for(auto entity : {startup, fixed, updated})
            entity.add_component<RigidBodyComponent>();
        fixed.add_component<ColliderComponent>().is_trigger = true;
        fixed.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{22});
        fixed.add_component<AudioSourceComponent>().clip = AssetHandle{33};
        fixed.set_transform({.translation = {1, 2, 3}});
        const auto uuid = fixed.get_uuid();
        const auto id = fixed.get_id();
        const auto child = scene.create_entity("Child");
        ASSERT_TRUE(scene.set_parent(child, fixed));

        auto* requester = add("requester");
        auto* observer = add("observer");
        requester->start = [&](Scene& current) {
            EXPECT_TRUE(current.request_remove_rigid_body(startup));
            return UpdateResult::success();
        };
        observer->start = [&](Scene&) {
            EXPECT_TRUE(startup.has_component<RigidBodyComponent>());
            return UpdateResult::success();
        };
        requester->fixed = [&](Scene& current, const System::Context&) {
            EXPECT_TRUE(current.request_remove_rigid_body(fixed));
            return UpdateResult::success();
        };
        observer->fixed = [&](Scene&, const System::Context&) {
            EXPECT_FALSE(startup.has_component<RigidBodyComponent>());
            EXPECT_TRUE(fixed.has_component<RigidBodyComponent>());
            return UpdateResult::success();
        };
        requester->update_frame = [&](Scene& current, const System::Context&) {
            EXPECT_TRUE(current.request_remove_rigid_body(updated));
            return UpdateResult::success();
        };
        observer->update_frame = [&](Scene&, const System::Context&) {
            EXPECT_FALSE(fixed.has_component<RigidBodyComponent>());
            EXPECT_TRUE(updated.has_component<RigidBodyComponent>());
            return UpdateResult::success();
        };

        ASSERT_TRUE(runtime.start(scene, State::Paused));
        EXPECT_FALSE(startup.has_component<RigidBodyComponent>());
        advance(1);
        EXPECT_TRUE(fixed.has_component<RigidBodyComponent>());
        EXPECT_TRUE(updated.has_component<RigidBodyComponent>());
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_FALSE(fixed.has_component<RigidBodyComponent>());
        EXPECT_FALSE(updated.has_component<RigidBodyComponent>());
        EXPECT_EQ(fixed.get_uuid(), uuid);
        EXPECT_EQ(fixed.get_id(), id);
        EXPECT_EQ(fixed.get_component<NameComponent>().name, "Fixed");
        EXPECT_EQ(fixed.get_component<TransformComponent>().translation, Math::Vec3(1, 2, 3));
        ASSERT_TRUE(fixed.has_component<ColliderComponent>());
        EXPECT_TRUE(fixed.get_component<ColliderComponent>().is_trigger);
        ASSERT_TRUE(fixed.has_component<MeshRendererComponent>());
        EXPECT_EQ(fixed.get_component<MeshRendererComponent>().mesh, AssetHandle{11});
        EXPECT_EQ(fixed.get_component<MeshRendererComponent>().material, AssetHandle{22});
        ASSERT_TRUE(fixed.has_component<AudioSourceComponent>());
        EXPECT_EQ(fixed.get_component<AudioSourceComponent>().clip, AssetHandle{33});
        EXPECT_EQ(scene.get_parent(child), fixed);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(fixed.has_component<RigidBodyComponent>());
    }

    TEST_F(SceneRuntimeTest, RigidBodyRemovalValidatesTargetsAndSharesAnIdempotentQueueBudget) {
        auto queued = scene.create_entity("Queued");
        auto rejected = scene.create_entity("Rejected");
        queued.add_component<RigidBodyComponent>();
        rejected.add_component<RigidBodyComponent>();
        const auto without_body = scene.create_entity("No body");
        const auto stale = scene.create_entity("Destroyed");
        scene.destroy_entity(stale);
        Scene other;
        auto foreign = other.create_entity();
        foreign.add_component<RigidBodyComponent>();
        EXPECT_FALSE(scene.request_remove_rigid_body(queued));
        EXPECT_FALSE(scene.request_remove_rigid_body(without_body));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(scene.request_remove_rigid_body({}));
        EXPECT_FALSE(scene.request_remove_rigid_body(stale));
        EXPECT_FALSE(scene.request_remove_rigid_body(foreign));
        for(int index = 0; index < 1024; ++index) {
            ASSERT_TRUE(scene.request_remove_rigid_body(queued));
            ASSERT_TRUE(scene.request_remove_rigid_body(without_body));
        }
        for(int index = 0; index < 1023; ++index)
            ASSERT_TRUE(scene.request_create_entity());
        EXPECT_FALSE(scene.request_create_entity("Full"));
        EXPECT_FALSE(scene.request_remove_rigid_body(rejected));
        EXPECT_TRUE(scene.request_remove_rigid_body(queued));
        EXPECT_TRUE(scene.request_remove_rigid_body(without_body));
        EXPECT_TRUE(queued.has_component<RigidBodyComponent>());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(queued.has_component<RigidBodyComponent>());
        EXPECT_TRUE(rejected.has_component<RigidBodyComponent>());
        EXPECT_TRUE(foreign.has_component<RigidBodyComponent>());
        EXPECT_TRUE(scene.request_remove_rigid_body(queued));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.request_remove_rigid_body(queued));
    }

    TEST_F(SceneRuntimeTest, RigidBodyRemovalIgnoresRecreatedTargetsAndAlreadyRemovedComponents) {
        auto original = scene.create_entity("Original");
        auto removed = scene.create_entity("Already removed");
        auto destroy_first = scene.create_entity("Destroy first");
        auto remove_first = scene.create_entity("Remove first");
        for(auto entity : {original, removed, destroy_first, remove_first})
            entity.add_component<RigidBodyComponent>();
        const auto uuid = original.get_uuid();
        const auto original_id = original.get_id();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.request_remove_rigid_body(original));
        scene.destroy_entity(original);
        auto replacement = scene.create_entity_with_uuid(uuid, "Replacement");
        ASSERT_TRUE(replacement);
        ASSERT_NE(replacement.get_id(), original_id);
        replacement.add_component<RigidBodyComponent>();
        EXPECT_FALSE(scene.request_remove_rigid_body(original));
        ASSERT_TRUE(scene.request_remove_rigid_body(removed));
        removed.remove_component<RigidBodyComponent>();
        ASSERT_TRUE(scene.request_destroy_entity(destroy_first));
        ASSERT_TRUE(scene.request_remove_rigid_body(destroy_first));
        ASSERT_TRUE(scene.request_remove_rigid_body(remove_first));
        ASSERT_TRUE(scene.request_destroy_entity(remove_first));

        ASSERT_TRUE(runtime.advance(0));
        EXPECT_TRUE(replacement.has_component<RigidBodyComponent>());
        EXPECT_EQ(scene.find_entity(uuid), replacement);
        EXPECT_TRUE(scene.is_valid(removed));
        EXPECT_FALSE(removed.has_component<RigidBodyComponent>());
        EXPECT_FALSE(scene.is_valid(destroy_first));
        EXPECT_FALSE(scene.is_valid(remove_first));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, StopDiscardsPendingRigidBodyRemovalAndStopCallbackRequests) {
        auto pending = scene.create_entity("Pending");
        auto during_stop = scene.create_entity("During stop");
        pending.add_component<RigidBodyComponent>();
        during_stop.add_component<RigidBodyComponent>();
        auto* requester = add();
        requester->stop = [&](Scene& current) {
            EXPECT_TRUE(current.request_remove_rigid_body(during_stop));
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.request_remove_rigid_body(pending));
        ASSERT_TRUE(runtime.stop());
        EXPECT_TRUE(pending.has_component<RigidBodyComponent>());
        EXPECT_TRUE(during_stop.has_component<RigidBodyComponent>());
        ASSERT_TRUE(runtime.start(scene));
        advance(0.1);
        EXPECT_TRUE(pending.has_component<RigidBodyComponent>());
        EXPECT_TRUE(during_stop.has_component<RigidBodyComponent>());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, FailedPhasesDiscardPendingRigidBodyRemovalBeforeRestart) {
        auto target = scene.create_entity("Target");
        target.add_component<RigidBodyComponent>();
        auto* requester = add("requester");
        auto* failing = add("failing");
        for(int phase = 0; phase < 3; ++phase) {
            const auto request = [&](Scene& current) {
                EXPECT_TRUE(current.request_remove_rigid_body(target));
                return UpdateResult::success();
            };
            const auto fail = [&](Scene&) {
                EXPECT_TRUE(target.has_component<RigidBodyComponent>());
                return UpdateResult::failure({"phase failed"});
            };
            if(phase == 0) {
                requester->start = request;
                failing->start = fail;
                EXPECT_FALSE(runtime.start(scene));
            } else {
                auto& request_callback = phase == 1 ? requester->fixed : requester->update_frame;
                auto& fail_callback = phase == 1 ? failing->fixed : failing->update_frame;
                request_callback = [&](Scene& current, const System::Context&) {
                    return request(current);
                };
                fail_callback = [&](Scene& current, const System::Context&) {
                    return fail(current);
                };
                ASSERT_TRUE(runtime.start(scene));
                EXPECT_FALSE(runtime.advance(phase == 1 ? 0.1 : 0));
            }
            EXPECT_FALSE(runtime.is_active());
            EXPECT_TRUE(target.has_component<RigidBodyComponent>());
            requester->start = {};
            requester->fixed = {};
            requester->update_frame = {};
            failing->start = {};
            failing->fixed = {};
            failing->update_frame = {};
            ASSERT_TRUE(runtime.start(scene));
            advance(0.1);
            EXPECT_TRUE(target.has_component<RigidBodyComponent>());
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(SceneRuntimeTest, SessionValuesExistOnlyWhileRuntimeIsActive) {
        EXPECT_FALSE(runtime.get_session().set_value("game.score", 1.0f));
        EXPECT_FALSE(runtime.get_session().get_value("game.score"));
        add();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.get_session().set_value("game.score", 1.0f));
        ASSERT_TRUE(runtime.get_session().set_value("game.complete", false));
        ASSERT_TRUE(runtime.get_session().set_value("game.note", std::string("ready")));
        ASSERT_TRUE(runtime.get_session().set_value("game.spawn", Math::Vec3(1, 2, 3)));
        EXPECT_EQ(std::get<float>(*runtime.get_session().get_value("game.score")), 1.0f);
        SceneRuntime another_runtime;
        EXPECT_FALSE(another_runtime.start(scene));
        EXPECT_EQ(std::get<float>(*runtime.get_session().get_value("game.score")), 1.0f);
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        const auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized);
        EXPECT_EQ(serialized.value().find("game.score"), std::string::npos);
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone);
        SceneRuntime clone_runtime;
        ASSERT_TRUE(clone_runtime.start(*clone.value()));
        EXPECT_FALSE(clone_runtime.get_session().get_value("game.score"));
        ASSERT_TRUE(clone_runtime.stop());
        EXPECT_FALSE(
            runtime.get_session().set_value("game.score", std::numeric_limits<float>::infinity()));
        EXPECT_FALSE(runtime.get_session().set_value("", 1.0f));
        EXPECT_FALSE(runtime.get_session().set_value(std::string(129, 'x'), 1.0f));
        EXPECT_FALSE(runtime.get_session().set_value(std::string_view("bad\0key", 7), 1.0f));
        EXPECT_FALSE(runtime.get_session().set_value("game.score", EntityUuid::generate()));
        EXPECT_FALSE(runtime.get_session().set_value(
            "game.spawn", Math::Vec3(std::numeric_limits<float>::quiet_NaN(), 0, 0)));
        EXPECT_EQ(std::get<Math::Vec3>(*runtime.get_session().get_value("game.spawn")),
            Math::Vec3(1, 2, 3));
        EXPECT_FALSE(runtime.get_session().set_value("game.note", std::string(4097, 'x')));
        EXPECT_EQ(std::get<std::string>(*runtime.get_session().get_value("game.note")), "ready");
        ASSERT_TRUE(runtime.set_state(State::Paused));
        advance(1);
        EXPECT_EQ(std::get<float>(*runtime.get_session().get_value("game.score")), 1.0f);
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_EQ(std::get<float>(*runtime.get_session().get_value("game.score")), 1.0f);
        EXPECT_TRUE(runtime.get_session().erase_value("game.note"));
        EXPECT_FALSE(runtime.get_session().get_value("game.note"));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(runtime.get_session().get_value("game.score"));
        EXPECT_FALSE(runtime.get_session().set_value("game.score", 3.0f));
        ASSERT_TRUE(another_runtime.start(scene));
        EXPECT_FALSE(another_runtime.get_session().get_value("game.score"));
        ASSERT_TRUE(another_runtime.stop());
    }

    TEST_F(SceneRuntimeTest, SessionStateHasBoundedKeysAndPreservesExistingValues) {
        ASSERT_TRUE(runtime.start(scene));
        for(int index = 0; index < 128; ++index)
            ASSERT_TRUE(
                runtime.get_session().set_value("key." + std::to_string(index), float(index)));
        EXPECT_FALSE(runtime.get_session().set_value("overflow", 1.0f));
        EXPECT_TRUE(runtime.get_session().set_value("key.0", 9.0f));
        EXPECT_EQ(std::get<float>(*runtime.get_session().get_value("key.0")), 9.0f);
        EXPECT_FALSE(runtime.get_session().get_value("overflow"));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, ConcurrentSessionsIsolateValuesControlRequestsAndRestart) {
        Scene other_scene;
        Calls other_calls;
        SceneRuntime other_runtime;
        auto actions = InputActions::create(
            {{"jump", InputActions::Type::Button, {{Input::Key::Space}}, "gameplay"}},
            {{"gameplay", true}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(actions.value()));
        ASSERT_TRUE(other_runtime.set_input_actions(actions.value()));
        bool jump = false;
        bool other_jump = false;
        auto other_reader = std::make_unique<RecordingSystem>(other_calls, "other");
        other_reader->update_frame = [&](Scene& current, const System::Context& context) {
            EXPECT_TRUE(context.session.is_bound_to(current));
            EXPECT_EQ(&context.session, &other_runtime.get_session());
            other_jump = context.input.action("jump")->down;
            return UpdateResult::success();
        };
        ASSERT_TRUE(other_runtime.add_system(std::move(other_reader)));
        auto* reader = add();
        reader->update_frame = [&](Scene& current, const System::Context& context) {
            EXPECT_TRUE(context.session.is_bound_to(current));
            EXPECT_EQ(&context.session, &runtime.get_session());
            jump = context.input.action("jump")->down;
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(other_runtime.start(other_scene));
        auto& session = runtime.get_session();
        auto& other_session = other_runtime.get_session();
        ASSERT_TRUE(session.set_value("score", 1.0f));
        ASSERT_TRUE(other_session.set_value("score", 2.0f));
        ASSERT_TRUE(session.request_restart());
        ASSERT_TRUE(session.request_input_context("gameplay", false));
        input.key_event(Input::Key::Space, true);
        advance(0);
        EXPECT_FALSE(jump);
        ASSERT_TRUE(other_runtime.advance(0, &input.get_frame()));
        EXPECT_TRUE(other_jump);
        EXPECT_FALSE(other_runtime.take_restart_request());
        EXPECT_TRUE(runtime.take_restart_request());

        // Stop 丢弃尚未消费的控制请求；新一局恢复默认输入组。
        ASSERT_TRUE(session.request_restart());
        ASSERT_TRUE(session.request_input_context("missing", true));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(session.is_active());
        EXPECT_FALSE(session.get_value("score"));
        EXPECT_FALSE(session.request_restart());
        EXPECT_FALSE(session.request_input_context("gameplay", true));
        EXPECT_FALSE(runtime.take_restart_request());
        EXPECT_EQ(other_session.get_value("score"), ParameterValue(2.0f));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(session.get_value("score"));
        advance(0);
        EXPECT_TRUE(jump);
        EXPECT_TRUE(other_session.is_bound_to(other_scene));
    }

    TEST_F(SceneRuntimeTest, InputInterruptionDiscardsPendingPressButPreservesReleaseAndClock) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::W, true);
        input.scroll_event({1, 2});
        const auto frame = input.publish_frame();
        ASSERT_TRUE(runtime.advance(0.01, &frame));
        ASSERT_TRUE(calls.fixed.empty());
        const auto timing = runtime.get_timing();
        ASSERT_TRUE(runtime.discard_input());
        ASSERT_TRUE(runtime.discard_input());
        EXPECT_EQ(runtime.get_timing().frame_index, timing.frame_index);
        EXPECT_EQ(runtime.get_timing().total_time, timing.total_time);
        ASSERT_TRUE(runtime.advance(0.09));
        ASSERT_EQ(calls.fixed.size(), 1);
        EXPECT_FALSE(calls.fixed[0].input.key(Input::Key::W).pressed);
        EXPECT_TRUE(calls.fixed[0].input.key(Input::Key::W).released);
        EXPECT_EQ(calls.fixed[0].input.scroll, Math::Vec2(0));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FALSE(calls.fixed[1].input.key(Input::Key::W).released);
    }

    TEST_F(SceneRuntimeTest, AccumulatesZeroStepInputAndConsumesEdgesOnlyOnTheFirstFixedStep) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::Space, true);
        input.cursor_event({0, 0});
        input.cursor_event({2, 3});
        input.scroll_event({0, 1});
        advance(0.03);
        EXPECT_TRUE(calls.fixed.empty());
        input.key_event(Input::Key::Space, false);
        input.cursor_event({5, 7});
        input.scroll_event({0, 2});
        advance(0.03);
        EXPECT_TRUE(calls.fixed.empty());
        advance(0.15);
        ASSERT_EQ(calls.fixed.size(), 2u);
        const auto& first = calls.fixed[0].input;
        EXPECT_TRUE(first.key(Input::Key::Space).pressed);
        EXPECT_TRUE(first.key(Input::Key::Space).released);
        EXPECT_FALSE(first.key(Input::Key::Space).down);
        EXPECT_EQ(first.cursor_delta, Math::Vec2(5, 7));
        EXPECT_EQ(first.scroll, Math::Vec2(0, 3));
        EXPECT_FALSE(calls.fixed[1].input.key(Input::Key::Space).pressed);
        EXPECT_FALSE(calls.fixed[1].input.key(Input::Key::Space).released);
        EXPECT_EQ(calls.fixed[1].input.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(calls.fixed[1].input.scroll, Math::Vec2(0));
        ASSERT_EQ(calls.updates.size(), 3u);
        EXPECT_TRUE(calls.updates[0].input.key(Input::Key::Space).pressed);
        EXPECT_TRUE(calls.updates[1].input.key(Input::Key::Space).released);
        EXPECT_FALSE(calls.updates[2].input.key(Input::Key::Space).pressed);
    }

    TEST_F(SceneRuntimeTest, DuplicateFramesDoNotReplayInputAndEachSystemSeesTheSameEdges) {
        add("A");
        add("B");
        ASSERT_TRUE(runtime.start(scene));
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.scroll_event({0, 1});
        const auto frame = input.publish_frame();
        ASSERT_TRUE(runtime.advance(0.02, &frame));
        ASSERT_TRUE(runtime.advance(0.18, &frame));
        ASSERT_EQ(calls.fixed.size(), 4u);
        for(size_t index = 0; index < 2; ++index) {
            EXPECT_TRUE(calls.fixed[index].input.mouse(Input::MouseButton::Right).pressed);
            EXPECT_EQ(calls.fixed[index].input.scroll.y, 1);
            EXPECT_TRUE(calls.fixed[index + 2].input.mouse(Input::MouseButton::Right).down);
            EXPECT_FALSE(calls.fixed[index + 2].input.mouse(Input::MouseButton::Right).pressed);
            EXPECT_FALSE(calls.updates[index + 2].input.mouse(Input::MouseButton::Right).pressed);
            EXPECT_EQ(calls.updates[index + 2].input.scroll, Math::Vec2(0));
        }
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Right).pressed);
    }

    TEST_F(SceneRuntimeTest, MissingInputAndFocusLossCancelPendingPressesButPreserveRelease) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        advance(0.04);
        ASSERT_TRUE(runtime.advance(0.02));
        ASSERT_TRUE(runtime.advance(0.04));
        ASSERT_EQ(calls.fixed.size(), 1u);
        EXPECT_FALSE(calls.fixed.back().input.focused);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::W).released);
        EXPECT_EQ(calls.fixed.back().input.scroll, Math::Vec2(0));
        EXPECT_TRUE(calls.updates[1].input.key(Input::Key::W).released);
        EXPECT_FALSE(calls.updates[2].input.key(Input::Key::W).released);
        EXPECT_EQ(runtime.get_timing().frame_index, 3u);

        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::Space, true);
        advance(0.04);
        input.focus_event(false);
        advance(0.06);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::Space).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::Space).released);
    }

    TEST_F(SceneRuntimeTest, DisconnectAndInputSourceRestartDoNotReplayStalePendingEdges) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        Input::GamepadSample pad;
        input.gamepad_sample(0, pad);
        advance(0);
        pad.buttons[0] = true;
        input.gamepad_sample(0, pad);
        advance(0.04);
        input.gamepad_sample(0, std::nullopt);
        advance(0.06);
        const auto& disconnected = calls.fixed.back().input.gamepads[0];
        EXPECT_FALSE(disconnected.connected);
        EXPECT_FALSE(disconnected.buttons[0].pressed);
        EXPECT_TRUE(disconnected.buttons[0].released);

        input.key_event(Input::Key::W, true);
        advance(0.04);
        Input replacement;
        replacement.focus_event(true);
        replacement.key_event(Input::Key::E, true);
        ASSERT_TRUE(runtime.advance(0.06, &replacement.publish_frame()));
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::E).pressed);
    }

    TEST_F(SceneRuntimeTest, BoundsCatchupAndReportsDiscardedTimeWithoutCarryingWholeSteps) {
        ASSERT_TRUE(runtime.set_settings(
            {.fixed_delta = 0.1, .max_frame_delta = 0.55, .max_fixed_steps = 2}));
        add();
        ASSERT_TRUE(runtime.start(scene));
        advance(2.0);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 2u);
        EXPECT_NEAR(runtime.get_timing().dropped_time, 1.75, 1e-9);
        EXPECT_NEAR(runtime.get_timing().interpolation, 0.5, 1e-9);
        EXPECT_DOUBLE_EQ(calls.updates.back().delta, 0.55);
        advance(0.05);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 1u);
        EXPECT_NEAR(runtime.get_timing().fixed_time, 0.3, 1e-9);
        EXPECT_NEAR(runtime.get_timing().interpolation, 0, 1e-9);
    }

    TEST_F(SceneRuntimeTest, EqualTimePartitionsGiveTheSameFixedSimulationWithoutOverload) {
        double position = 0;
        auto* system = add();
        system->fixed = [&](Scene&, const System::Context& context) {
            position += context.delta_time * 3;
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 10; ++i)
            advance(0.1);
        const auto reference = position;
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        position = 0;
        for(int i = 0; i < 100; ++i)
            advance(0.01);
        EXPECT_DOUBLE_EQ(position, reference);
        EXPECT_EQ(runtime.get_timing().fixed_index, 10u);
    }

    TEST_F(SceneRuntimeTest, StartupAndUpdateFailuresPreserveErrorAndCleanUpPartialSystems) {
        const Error failure{"system failure", std::make_error_code(std::errc::io_error)};
        add("A");
        auto* failing = add("B");
        add("C");
        failing->start = [&](Scene&) { return UpdateResult::failure(failure); };
        auto result = runtime.start(scene);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, failure.code);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(
            calls.order, (std::vector<std::string>{"start A", "start B", "stop B", "stop A"}));
        failing->start = {};
        for(bool fixed : {true, false}) {
            calls.order.clear();
            failing->fixed = {};
            failing->update_frame = {};
            auto fail = [&](Scene&, const System::Context&) {
                return UpdateResult::failure(failure);
            };
            if(fixed)
                failing->fixed = fail;
            else
                failing->update_frame = fail;
            ASSERT_TRUE(runtime.start(scene));
            result = runtime.advance(0.1);
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error().message, failure.message);
            EXPECT_EQ(result.error().code, failure.code);
            EXPECT_FALSE(runtime.is_active());
            EXPECT_EQ(calls.order[calls.order.size() - 3], "stop C");
            EXPECT_EQ(calls.order[calls.order.size() - 2], "stop B");
            EXPECT_EQ(calls.order.back(), "stop A");
        }
    }

    TEST_F(SceneRuntimeTest, InvalidSettingsAndReentryAreRejectedWithoutChangingTheActiveRun) {
        EXPECT_FALSE(runtime.set_state(State::Paused));
        EXPECT_FALSE(runtime.request_step());
        EXPECT_FALSE(runtime.set_settings({.fixed_delta = 0}));
        EXPECT_FALSE(runtime.set_settings({.max_fixed_steps = 0}));
        EXPECT_FALSE(
            runtime.set_settings({.max_frame_delta = std::numeric_limits<double>::infinity()}));
        EXPECT_FALSE(runtime.add_system(nullptr));
        auto* system = add();
        system->update_frame = [&](Scene& active, const System::Context&) {
            EXPECT_FALSE(runtime.start(active));
            EXPECT_FALSE(runtime.stop());
            EXPECT_FALSE(runtime.set_state(State::Paused));
            EXPECT_FALSE(runtime.request_step());
            EXPECT_FALSE(runtime.advance(0));
            EXPECT_FALSE(runtime.clear_systems());
            EXPECT_FALSE(runtime.set_settings({}));
            EXPECT_FALSE(runtime.add_system(std::make_unique<RecordingSystem>(calls, "nested")));
            return UpdateResult::success();
        };
        system->stop = [&](Scene& active) {
            EXPECT_EQ(&active, &scene);
            EXPECT_FALSE(runtime.stop());
        };
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.request_step());
        EXPECT_FALSE(runtime.set_state(static_cast<State>(99)));
        EXPECT_FALSE(runtime.advance(-1));
        EXPECT_FALSE(runtime.advance(std::numeric_limits<double>::quiet_NaN()));
        advance(0.1);
        EXPECT_TRUE(runtime.is_active());
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        ASSERT_TRUE(runtime.stop());
        system->stop = {};
    }

    TEST_F(SceneRuntimeTest, PauseFreezesSimulationAndResumeRebasesTimeAndInput) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::W, true);
        advance(0.04);
        ASSERT_TRUE(runtime.set_state(State::Paused));
        EXPECT_TRUE(runtime.is_active());
        const auto before = runtime.get_timing();
        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::Space, true);
        input.cursor_event({0, 0});
        input.cursor_event({9, 8});
        input.scroll_event({0, 4});
        advance(60);
        input.key_event(Input::Key::Space, false);
        advance(60);
        EXPECT_EQ(calls.order, (std::vector<std::string>{"start A", "update A"}));
        EXPECT_EQ(runtime.get_timing().frame_index, before.frame_index);
        EXPECT_EQ(runtime.get_timing().fixed_index, before.fixed_index);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, before.total_time);
        EXPECT_DOUBLE_EQ(runtime.get_timing().dropped_time, 0);
        EXPECT_DOUBLE_EQ(runtime.get_timing().interpolation, 0);

        ASSERT_TRUE(runtime.set_state(State::Running));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        advance(0.04);
        EXPECT_TRUE(calls.fixed.empty());
        EXPECT_TRUE(calls.updates.back().input.key(Input::Key::W).down);
        EXPECT_FALSE(calls.updates.back().input.key(Input::Key::W).pressed);
        EXPECT_EQ(calls.updates.back().input.scroll, Math::Vec2(0));
        input.key_event(Input::Key::W, false);
        advance(0.06);
        ASSERT_EQ(calls.fixed.size(), 1u);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::W).released);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::Space).pressed);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::Space).released);
        EXPECT_EQ(calls.fixed.back().input.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(calls.fixed.back().input.scroll, Math::Vec2(0));
        EXPECT_NEAR(runtime.get_timing().total_time, 0.14, 1e-9);
    }

    TEST_F(SceneRuntimeTest, PauseNotifiesInitialStateAndTransitionsAndRejectsReentry) {
        auto* first = add("A");
        auto* second = add("B");
        std::vector<std::string> transitions;
        first->pause = [&](bool paused) {
            transitions.push_back(paused ? "pause A" : "resume A");
            EXPECT_EQ(runtime.get_state(), paused ? State::Paused : State::Running);
            EXPECT_FALSE(runtime.set_state(State::Running));
            EXPECT_FALSE(runtime.stop());
            EXPECT_FALSE(runtime.advance(0));
            EXPECT_FALSE(runtime.request_step());
            EXPECT_FALSE(runtime.clear_systems());
            EXPECT_FALSE(runtime.discard_input());
        };
        second->pause = [&](bool paused) {
            transitions.push_back(paused ? "pause B" : "resume B");
        };
        EXPECT_FALSE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(transitions, (std::vector<std::string>{"resume A", "resume B"}));
        transitions.clear();
        ASSERT_TRUE(runtime.set_state(State::Running));
        EXPECT_TRUE(transitions.empty());
        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.set_state(State::Paused));
        advance(1);
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_EQ(transitions, (std::vector<std::string>{"pause A", "pause B"}));
        ASSERT_TRUE(runtime.set_state(State::Running));
        ASSERT_TRUE(runtime.set_state(State::Running));
        EXPECT_EQ(
            transitions, (std::vector<std::string>{"pause A", "pause B", "resume A", "resume B"}));
        ASSERT_TRUE(runtime.set_state(State::Paused));
        const auto before_stop = transitions.size();
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(transitions.size(), before_stop);
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(runtime.get_state(), State::Running);
        ASSERT_TRUE(runtime.set_state(State::Paused));
        EXPECT_EQ(transitions.size(), before_stop + 4);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, SingleStepRunsBothPhasesOnceAndCoalescesPendingRequests) {
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1, .max_frame_delta = 0.01}));
        add("A");
        add("B");
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.set_state(State::Paused));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.request_step());
        advance(10);
        EXPECT_EQ(calls.order, (std::vector<std::string>{"start A", "start B", "fixed A", "fixed B",
                                   "update A", "update B"}));
        EXPECT_EQ(runtime.get_state(), State::Paused);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 1u);
        EXPECT_EQ(runtime.get_timing().fixed_index, 1u);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, 0.1);
        EXPECT_DOUBLE_EQ(runtime.get_timing().fixed_time, 0.1);
        EXPECT_DOUBLE_EQ(runtime.get_timing().dropped_time, 0);
        for(const auto& sample : calls.fixed) {
            EXPECT_DOUBLE_EQ(sample.delta, 0.1);
            EXPECT_TRUE(sample.input.key(Input::Key::W).down);
            EXPECT_FALSE(sample.input.key(Input::Key::W).pressed);
            EXPECT_EQ(sample.input.scroll, Math::Vec2(0));
        }
        EXPECT_DOUBLE_EQ(calls.updates.front().delta, 0.1);
        advance(10);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 0u);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(runtime.get_timing().fixed_index, 2u);
        EXPECT_FALSE(calls.fixed.back().input.focused);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).down);
        EXPECT_EQ(calls.updates.size(), 4u);
    }

    TEST_F(SceneRuntimeTest, ResumeAndStopCancelPendingStepsAndRestartIsRunning) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        advance(0.06);
        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.set_state(State::Running));
        advance(0.04);
        EXPECT_TRUE(calls.fixed.empty());
        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(runtime.get_state(), State::Running);
        advance(0);
        EXPECT_TRUE(calls.fixed.empty());
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
    }

    TEST_F(SceneRuntimeTest, FailedSingleStepCleansUpAndCannotBeReplayed) {
        add("A");
        auto* failing = add("B");
        const Error failure{"step failed", std::make_error_code(std::errc::io_error)};
        failing->fixed = [&](Scene&, const System::Context&) {
            EXPECT_FALSE(runtime.set_state(State::Running));
            EXPECT_FALSE(runtime.request_step());
            return UpdateResult::failure(failure);
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.request_step());
        const auto result = runtime.advance(0);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, failure.code);
        EXPECT_EQ(result.error().message, failure.message);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(calls.order[calls.order.size() - 2], "stop B");
        EXPECT_EQ(calls.order.back(), "stop A");
        EXPECT_TRUE(calls.updates.empty());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(calls.fixed.size(), 2u);
    }

    TEST_F(SceneRuntimeTest, CameraRunsOncePerFrameNotOncePerFixedStep) {
        auto camera = scene.create_entity("Camera");
        camera.add_component<CameraComponent>().primary = true;
        camera.add_component<CameraControllerComponent>();
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        auto actions = InputActions::create(
            {{"camera.move_z", InputActions::Type::Axis, {{Input::Key::W, -1}}},
                {"camera.zoom", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        ASSERT_TRUE(runtime.add_system(std::make_unique<CameraControllerSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 1});
        advance(0.05);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 5u);
        EXPECT_NEAR(camera.get_component<TransformComponent>().translation.z, -0.35f, 1e-6f);
        ASSERT_TRUE(runtime.advance(0.05));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 5u);
        EXPECT_NEAR(camera.get_component<TransformComponent>().translation.z, -0.35f, 1e-6f);
        advance(10);
        EXPECT_NEAR(camera.get_component<TransformComponent>().translation.z, -1.10f, 1e-6f);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, 0.35);
    }

    TEST_F(SceneRuntimeTest, CameraLookRateFollowsPauseStepAndLiveRebinding) {
        const auto action_id = Uuid::generate();
        const auto binding_id = Uuid::generate();
        const auto actions = InputActions::create(
            {{"camera.look_rate_x", InputActions::Type::Axis,
                {{Input::GamepadAxis::RightX, 1, 0, binding_id}}, "camera", action_id}},
            {{"camera", true}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(actions.value()));
        auto camera = scene.create_entity("Camera");
        camera.add_component<CameraComponent>().primary = true;
        camera.add_component<CameraControllerComponent>();
        const auto& transform = camera.get_component<TransformComponent>();
        ASSERT_TRUE(runtime.add_system(std::make_unique<CameraControllerSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        Input::GamepadSample pad;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 1;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::LeftX)] = -1;
        input.gamepad_sample(0, pad);
        advance(0.2);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 2u);
        EXPECT_EQ(transform.rotation, Math::Vec3(0, -24, 0));
        EXPECT_FALSE(runtime.wants_cursor_capture());

        ASSERT_TRUE(runtime.set_state(State::Paused));
        advance(2);
        EXPECT_EQ(transform.rotation, Math::Vec3(0, -24, 0));
        ASSERT_TRUE(runtime.request_step());
        advance(2);
        EXPECT_EQ(transform.rotation, Math::Vec3(0, -36, 0));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 1u);
        ASSERT_TRUE(runtime.get_session().request_input_context("camera", false));
        advance(0);
        auto bindings = actions.value().actions();
        bindings.front().bindings.front().control = Input::GamepadAxis::LeftX;
        const auto rebound = InputActions::create(std::move(bindings), actions.value().contexts());
        ASSERT_TRUE(rebound);
        ASSERT_TRUE(runtime.rebind_input_actions(rebound.value()));
        advance(0);
        ASSERT_TRUE(runtime.request_step());
        advance(1);
        EXPECT_EQ(transform.rotation, Math::Vec3(0, -36, 0));
        ASSERT_TRUE(runtime.get_session().request_input_context("camera", true));
        ASSERT_TRUE(runtime.request_step());
        advance(0);
        EXPECT_EQ(transform.rotation, Math::Vec3(0, -24, 0));
        EXPECT_EQ(runtime.get_state(), State::Paused);
        EXPECT_FALSE(runtime.wants_cursor_capture());

        ASSERT_TRUE(runtime.set_state(State::Running));
        advance(0.1);
        EXPECT_EQ(transform.rotation, Math::Vec3(0, -12, 0));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_EQ(transform.rotation, Math::Vec3(0, -12, 0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, StoredPlayerBindingsComposeWithNewDefaultsBeforeRuntimeStarts) {
        TemporaryDirectory directory;
        const auto project_id = Uuid::generate();
        const auto action_id = Uuid::generate();
        const auto binding_id = Uuid::generate();
        const auto original = InputActions::create({{"jump", InputActions::Type::Button,
            {{Input::Key::Space, 1, 0, binding_id}}, {}, action_id}});
        ASSERT_TRUE(original);
        auto settings = PlayerInputSettings::load(project_id, directory.path() / "input.json");
        ASSERT_TRUE(settings);
        const auto overrides = InputOverrides::create({{action_id, InputActions::Type::Button,
            false, {{.id = binding_id, .control = Input::Key::K}}}});
        ASSERT_TRUE(overrides);
        ASSERT_TRUE(settings.value().save(overrides.value()));
        auto upgraded_actions = original.value().actions();
        upgraded_actions.front().bindings.push_back({Input::Key::J, 1, 0, Uuid::generate()});
        const auto defaults = InputActions::create(std::move(upgraded_actions));
        ASSERT_TRUE(defaults);
        const auto reopened = PlayerInputSettings::load(project_id, settings.value().path());
        ASSERT_TRUE(reopened);
        auto resolved = reopened.value().overrides().resolve(defaults.value());
        ASSERT_TRUE(resolved);
        EXPECT_TRUE(resolved.value().issues.empty());
        ASSERT_TRUE(runtime.set_input_actions(std::move(resolved).value().actions));
        std::vector<InputState::Action> received;
        add()->update_frame = [&](Scene&, const System::Context& context) {
            received.push_back(*context.input.action("jump"));
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::Space, true);
        advance(0.1);
        ASSERT_EQ(received.size(), 1u);
        EXPECT_FALSE(received.back().down);
        input.key_event(Input::Key::K, true);
        advance(0.1);
        EXPECT_TRUE(received.back().pressed);
        input.key_event(Input::Key::K, false);
        advance(0.1);
        EXPECT_TRUE(received.back().released);
        input.key_event(Input::Key::J, true);
        advance(0.1);
        EXPECT_TRUE(received.back().pressed);
        EXPECT_FALSE(runtime.set_input_actions(defaults.value()));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(settings.value().save(InputOverrides{}));
        const auto restored = PlayerInputSettings::load(project_id, settings.value().path());
        ASSERT_TRUE(restored);
        auto inherited = restored.value().overrides().resolve(defaults.value());
        ASSERT_TRUE(inherited);
        EXPECT_EQ(inherited.value().actions, defaults.value());
        ASSERT_TRUE(runtime.set_input_actions(std::move(inherited).value().actions));
    }

    TEST_F(SceneRuntimeTest, ActionsFollowFixedConsumptionPauseAndRebindingBoundaries) {
        auto actions =
            InputActions::create({{"jump", InputActions::Type::Button, {{Input::Key::Space}}},
                {"look", InputActions::Type::Delta, {{InputActions::Motion::ScrollY}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(actions.value()));
        std::vector<InputState::Action> fixed;
        std::vector<InputState::Action> updates;
        std::vector<float> deltas;
        auto* system = add();
        system->fixed = [&](Scene&, const System::Context& context) {
            fixed.push_back(*context.input.action("jump"));
            deltas.push_back(context.input.action("look")->value);
            return UpdateResult::success();
        };
        system->update_frame = [&](Scene&, const System::Context& context) {
            updates.push_back(*context.input.action("jump"));
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.set_input_actions(actions.value()));
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.scroll_event({0, 2});
        advance(0.01);
        EXPECT_TRUE(fixed.empty());
        EXPECT_TRUE(updates.back().pressed);
        advance(0.2);
        ASSERT_EQ(fixed.size(), 2u);
        EXPECT_TRUE(fixed[0].pressed);
        EXPECT_TRUE(fixed[0].released);
        EXPECT_FALSE(fixed[1].pressed);
        EXPECT_FALSE(fixed[1].released);
        EXPECT_EQ(deltas, (std::vector<float>{2, 0}));
        EXPECT_FALSE(updates.back().pressed);
        ASSERT_TRUE(runtime.set_state(State::Paused));
        input.key_event(Input::Key::Space, true);
        input.scroll_event({0, 10});
        advance(0.2);
        ASSERT_TRUE(runtime.request_step());
        advance(0.2);
        EXPECT_TRUE(fixed.back().down);
        EXPECT_FALSE(fixed.back().pressed);
        EXPECT_FLOAT_EQ(deltas.back(), 0);
        ASSERT_TRUE(runtime.set_state(State::Running));
        advance(0.1);
        EXPECT_FALSE(fixed.back().pressed);
        EXPECT_TRUE(fixed.back().down);
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_TRUE(fixed.back().released);
        EXPECT_TRUE(updates.back().released);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.set_input_actions({}));
    }

    TEST_F(SceneRuntimeTest, RestartInputRebasesTheFirstAuthorizedFrameWithoutReplayingEdges) {
        auto actions =
            InputActions::create({{"restart", InputActions::Type::Button, {{Input::Key::R}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        std::vector<InputState::Action> updates;
        std::vector<InputState::Action> fixed;
        auto* system = add();
        system->fixed = [&](Scene&, const System::Context& context) {
            fixed.push_back(*context.input.action("restart"));
            return UpdateResult::success();
        };
        system->update_frame = [&](Scene&, const System::Context& context) {
            updates.push_back(*context.input.action("restart"));
            return UpdateResult::success();
        };
        Input::Gate gate;
        for(int index = 0; index < 20; ++index)
            input.publish_frame();
        gate.read(input.publish_frame(), true);
        input.key_event(Input::Key::R, true);
        const auto& pressed = gate.read(input.publish_frame(), true);
        ASSERT_TRUE(pressed.key(Input::Key::R).pressed);
        ASSERT_TRUE(runtime.start(scene, State::Running, SceneRuntime::InputStart::Rebase));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FALSE(updates.back().pressed);
        ASSERT_TRUE(runtime.advance(0.1, &pressed));
        EXPECT_TRUE(updates.back().down);
        EXPECT_FALSE(updates.back().pressed);
        EXPECT_TRUE(fixed.back().down);
        EXPECT_FALSE(fixed.back().pressed);
        ASSERT_TRUE(runtime.advance(0.1, &pressed));
        EXPECT_FALSE(updates.back().pressed);
        input.key_event(Input::Key::R, false);
        const auto& released = gate.read(input.publish_frame(), true);
        ASSERT_TRUE(runtime.advance(0.1, &released));
        EXPECT_TRUE(updates.back().released);
        EXPECT_TRUE(fixed.back().released);
        input.key_event(Input::Key::R, true);
        const auto& next_press = gate.read(input.publish_frame(), true);
        ASSERT_TRUE(runtime.advance(0.1, &next_press));
        EXPECT_TRUE(updates.back().pressed);
        EXPECT_TRUE(fixed.back().pressed);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(SceneRuntimeTest, ActionMappingCannotBypassGateOrReplayDeniedPendingPress) {
        auto actions =
            InputActions::create({{"jump", InputActions::Type::Button, {{Input::Key::Space}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        std::vector<InputState::Action> fixed;
        std::vector<InputState::Action> updates;
        auto* system = add();
        system->fixed = [&](Scene&, const System::Context& context) {
            fixed.push_back(*context.input.action("jump"));
            return UpdateResult::success();
        };
        system->update_frame = [&](Scene&, const System::Context& context) {
            updates.push_back(*context.input.action("jump"));
            return UpdateResult::success();
        };
        Input::Gate gate;
        auto routed = [&](double delta, bool enabled) {
            const auto& frame = gate.read(input.publish_frame(), enabled);
            ASSERT_TRUE(runtime.advance(delta, &frame));
        };
        ASSERT_TRUE(runtime.start(scene));
        routed(0, true);
        input.key_event(Input::Key::Space, true);
        routed(0.01, true);
        EXPECT_TRUE(updates.back().pressed);
        EXPECT_TRUE(fixed.empty());
        routed(0.1, false);
        EXPECT_FALSE(fixed.back().pressed);
        EXPECT_FALSE(fixed.back().down);
        EXPECT_TRUE(updates.back().released);
        routed(0.1, true);
        EXPECT_FALSE(fixed.back().down);
        EXPECT_FALSE(updates.back().pressed);
        input.key_event(Input::Key::Space, false);
        routed(0.1, true);
        input.key_event(Input::Key::Space, true);
        routed(0.1, true);
        EXPECT_TRUE(fixed.back().pressed);
        EXPECT_TRUE(updates.back().pressed);
        ASSERT_TRUE(runtime.stop());
        auto replacement = InputActions::create({{"other", InputActions::Type::Button, {}}});
        ASSERT_TRUE(replacement);
        ASSERT_TRUE(runtime.set_input_actions(std::move(replacement).value()));
        system->fixed = {};
        system->update_frame = [&](Scene&, const System::Context& context) {
            EXPECT_EQ(context.input.action("jump"), nullptr);
            EXPECT_NE(context.input.action("other"), nullptr);
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        routed(0, true);
    }
}
