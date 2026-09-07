#include "runtime/native_script.h"
#include "demo/scripts.h"
#include "scene/scene_serializer.h"
#include "command_history.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <stdexcept>

namespace Comet::Tests {
    class NativeScriptTest: public ::testing::Test {
    protected:
        struct Data: ScriptComponent {
            float speed = 2;
        };
        struct Event {
            char phase;
            EntityUuid entity;
        };
        struct Control {
            bool fail_start = false;
            bool fail_update = false;
            std::optional<EntityUuid> remove_entity;
        };
        struct Probe final: NativeScript {
            std::vector<Event>* events = nullptr;
            Control* control = nullptr;
            EntityUuid owner;
            void on_start(Scene&, Entity entity) override {
                owner = entity.get_uuid();
                events->push_back({'s', owner});
                if(control->fail_start)
                    throw std::runtime_error("script start failed");
            }
            void fixed_update(
                Scene& scene, Entity entity, const System::Context& context) override {
                events->push_back({'f', owner});
                if(control->fail_update)
                    throw std::runtime_error("script update failed");
                entity.get_component<TransformComponent>().translation.x +=
                    entity.get_component<Data>().speed * float(context.delta_time);
                if(control->remove_entity) {
                    if(auto removed = scene.find_entity(*control->remove_entity))
                        scene.destroy_entity(removed);
                }
            }
            void update(Scene&, Entity, const System::Context&) override {
                events->push_back({'u', owner});
            }
            void on_stop() noexcept override { events->push_back({'x', owner}); }
        };
        Scene scene;
        std::vector<Event> events;
        Control control;
        ComponentRegistry registry = create_scene_component_registry();
        SceneRuntime runtime{{.fixed_delta = 0.01}};

        void SetUp() override {
            auto descriptor =
                make_script_descriptor<Data, Probe>("test.script", "Test Script",
                    {make_property_descriptor("speed", "Speed", &Data::speed)});
            descriptor.create_script = [this] {
                auto script = std::make_unique<Probe>();
                script->events = &events;
                script->control = &control;
                return script;
            };
            ASSERT_TRUE(registry.register_component(std::move(descriptor)));
            runtime.add_system(std::make_unique<NativeScriptSystem>(registry));
        }
        Entity actor() {
            auto entity = scene.create_entity();
            entity.add_component<Data>();
            return entity;
        }
        size_t count(char phase) const {
            return std::ranges::count_if(
                events, [phase](const auto& event) { return event.phase == phase; });
        }
    };

    TEST_F(
        NativeScriptTest, StartsInStableEntityOrderUpdatesAndStopsWithoutScenePointers) {
        const auto first = actor();
        const auto second = actor();
        runtime.start(scene);
        ASSERT_EQ(count('s'), 2U);
        EXPECT_LT(events[0].entity, events[1].entity);
        runtime.advance(0.02, {});
        EXPECT_EQ(count('f'), 4U);
        EXPECT_EQ(count('u'), 2U);
        EXPECT_NEAR(
            first.get_component<TransformComponent>().translation.x, 0.04f, 1e-6f);
        runtime.stop();
        EXPECT_EQ(count('x'), 2U);
        EXPECT_EQ(events[events.size() - 2].entity, events[1].entity);
        EXPECT_EQ(events.back().entity, events[0].entity);
        runtime.stop();
        EXPECT_EQ(count('x'), 2U);
        runtime.start(scene);
        EXPECT_EQ(count('s'), 4U);
        EXPECT_TRUE(second);
    }

    TEST_F(NativeScriptTest, FieldsUseSameDescriptorTransactionSerializerAndClone) {
        auto entity = actor();
        const auto key = entity.get_component<Data>().get_instance_key();
        CometEditor::CommandHistory history;
        history.bind_scene(&scene);
        CometEditor::PropertyEditTransaction edit(history, registry);
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "test.script", "speed"}));
        ASSERT_TRUE(edit.preview(5.0f));
        ASSERT_TRUE(edit.commit());
        EXPECT_EQ(entity.get_component<Data>().get_instance_key(), key);
        runtime.start(scene);
        runtime.advance(0.01, {});
        EXPECT_NEAR(
            entity.get_component<TransformComponent>().translation.x, 0.05f, 1e-6f);
        ASSERT_TRUE(history.undo());
        runtime.advance(0.01, {});
        EXPECT_NEAR(
            entity.get_component<TransformComponent>().translation.x, 0.07f, 1e-6f);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(count('s'), 1U);
        const SceneSerializer serializer(registry);
        const auto yaml = serializer.serialize(scene);
        EXPECT_NE(yaml.find("test.script"), std::string::npos);
        EXPECT_EQ(yaml.find("instance_key"), std::string::npos);
        const auto clone = serializer.deserialize(yaml);
        const auto cloned = clone->find_entity(entity.get_uuid());
        EXPECT_FLOAT_EQ(cloned.get_component<Data>().speed, 5);
        EXPECT_NE(cloned.get_component<Data>().get_instance_key(), key);
        EXPECT_EQ(count('s'), 1U);
    }

    TEST_F(
        NativeScriptTest, RemoveAndReaddInOneFrameRestartsEvenIfStorageAddressIsReused) {
        auto entity = actor();
        runtime.start(scene);
        const auto old_key = entity.get_component<Data>().get_instance_key();
        entity.remove_component<Data>();
        auto& replacement = entity.add_component<Data>();
        EXPECT_NE(replacement.get_instance_key(), old_key);
        runtime.advance(0.01, {});
        EXPECT_EQ(count('x'), 1U);
        EXPECT_EQ(count('s'), 2U);
        EXPECT_EQ(count('f'), 1U);
        ASSERT_GE(events.size(), 3U);
        EXPECT_EQ(events[1].phase, 'x');
        EXPECT_EQ(events[2].phase, 's');
    }

    TEST_F(NativeScriptTest, AddRemoveAndEntityDestructionAreObservedAtPhaseBoundaries) {
        auto entity = scene.create_entity();
        runtime.start(scene);
        EXPECT_TRUE(events.empty());
        entity.add_component<Data>();
        runtime.advance(0, {});
        EXPECT_EQ(count('s'), 1U);
        EXPECT_EQ(count('u'), 1U);
        entity.remove_component<Data>();
        runtime.advance(0, {});
        EXPECT_EQ(count('x'), 1U);
        entity.add_component<Data>();
        runtime.advance(0, {});
        scene.destroy_entity(entity);
        runtime.advance(0, {});
        EXPECT_EQ(count('s'), 2U);
        EXPECT_EQ(count('x'), 2U);
        runtime.stop();
        EXPECT_EQ(count('x'), 2U);
    }

    TEST_F(
        NativeScriptTest, EarlierScriptCanDeleteLaterEntityWithoutCallingStaleInstance) {
        const auto first = actor();
        const auto second = actor();
        runtime.start(scene);
        control.remove_entity = std::max(first.get_uuid(), second.get_uuid());
        runtime.advance(0.01, {});
        EXPECT_EQ(count('f'), 1U);
        EXPECT_EQ(count('x'), 1U);
        EXPECT_EQ(count('u'), 1U);
        EXPECT_EQ(scene.entity_count(), 1U);
    }

    TEST_F(NativeScriptTest, SelfDeletionDefersObjectDestructionUntilCallbackReturns) {
        const auto entity = actor();
        control.remove_entity = entity.get_uuid();
        runtime.start(scene);
        runtime.advance(0.01, {});
        EXPECT_EQ(count('f'), 1U);
        EXPECT_EQ(count('u'), 0U);
        EXPECT_EQ(count('x'), 1U);
        EXPECT_FALSE(entity);
    }

    TEST_F(NativeScriptTest, PartialStartupAndUpdateFailureStopInstancesExactlyOnce) {
        actor();
        control.fail_start = true;
        EXPECT_THROW(runtime.start(scene), std::runtime_error);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(count('s'), 1U);
        EXPECT_EQ(count('x'), 1U);
        control.fail_start = false;
        runtime.start(scene);
        control.fail_update = true;
        EXPECT_THROW(runtime.advance(0.01, {}), std::runtime_error);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(count('x'), 2U);
        runtime.stop();
        EXPECT_EQ(count('x'), 2U);
    }

    TEST_F(
        NativeScriptTest, ComponentCopyStartsNewIdentityButMovePreservesStorageIdentity) {
        Data original;
        const auto key = original.get_instance_key();
        Data copy = original;
        EXPECT_NE(copy.get_instance_key(), key);
        Data moved = std::move(original);
        EXPECT_EQ(moved.get_instance_key(), key);
        const auto old_copy = copy.get_instance_key();
        copy = moved;
        EXPECT_NE(copy.get_instance_key(), old_copy);
        EXPECT_NE(copy.get_instance_key(), moved.get_instance_key());
    }

    TEST_F(NativeScriptTest, StructuralSnapshotRestoreCreatesFreshRuntimeLifetime) {
        auto entity = actor();
        const auto* descriptor = registry.find_component("test.script");
        const auto original = entity.get_component<Data>().get_instance_key();
        const auto snapshot = descriptor->capture_component(entity);
        runtime.start(scene);
        ASSERT_TRUE(descriptor->remove_component(entity));
        ASSERT_TRUE(descriptor->restore_component(entity, snapshot));
        EXPECT_NE(entity.get_component<Data>().get_instance_key(), original);
        runtime.advance(0, {});
        EXPECT_EQ(count('s'), 2U);
        EXPECT_EQ(count('x'), 1U);
    }

    TEST_F(NativeScriptTest, RegistrationRequiresBothScriptFactoryAndLifetimeAccessor) {
        auto invalid = make_component_descriptor<Data>("invalid", "Invalid", {});
        invalid.create_script = [] { return std::make_unique<Probe>(); };
        EXPECT_FALSE(registry.register_component(std::move(invalid)));
        runtime.clear_systems();
        auto local = create_scene_component_registry();
        auto null_factory = make_script_descriptor<Data, Probe>("null", "Null", {});
        null_factory.create_script = [] { return std::unique_ptr<NativeScript>{}; };
        ASSERT_TRUE(local.register_component(std::move(null_factory)));
        runtime.add_system(std::make_unique<NativeScriptSystem>(local));
        actor();
        EXPECT_THROW(runtime.start(scene), std::runtime_error);
        EXPECT_FALSE(runtime.is_active());
    }

    TEST_F(NativeScriptTest, DemoScriptFieldsDriveOnlyPlayCloneAndRespectPauseStep) {
        auto demo = CometDemo::create_component_registry();
        auto edit = std::make_unique<Scene>();
        auto entity = edit->create_entity();
        entity.add_component<CometDemo::SpinComponent>().speed = 90;
        const SceneSerializer serializer(demo);
        auto play = serializer.clone(*edit);
        SceneRuntime play_runtime{{.fixed_delta = 0.01}};
        play_runtime.add_system(std::make_unique<NativeScriptSystem>(demo));
        play_runtime.start(*play);
        play_runtime.set_state(SceneRuntime::State::Paused);
        play_runtime.advance(1, {});
        const auto clone = play->find_entity(entity.get_uuid());
        EXPECT_EQ(clone.get_component<TransformComponent>().rotation.y, 0);
        play_runtime.request_step();
        play_runtime.advance(1, {});
        EXPECT_NEAR(clone.get_component<TransformComponent>().rotation.y, 0.9f, 1e-5f);
        EXPECT_EQ(entity.get_component<TransformComponent>().rotation.y, 0);
        const auto* descriptor = demo.find_component("demo.spin");
        auto editable = clone;
        ASSERT_TRUE(descriptor->find_property("enabled")->assign_value(
            descriptor->get_component(editable), false));
        play_runtime.request_step();
        play_runtime.advance(1, {});
        EXPECT_NEAR(clone.get_component<TransformComponent>().rotation.y, 0.9f, 1e-5f);
        play_runtime.stop();
    }
}
